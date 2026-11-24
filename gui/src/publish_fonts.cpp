#include "publish_fonts.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include "ui.hpp"

namespace fcg::assets {
extern const unsigned char inter_regular[];
extern const std::size_t inter_regular_size;
extern const unsigned char inter_semibold[];
extern const std::size_t inter_semibold_size;
extern const unsigned char inter_italic[];
extern const std::size_t inter_italic_size;
}  // namespace fcg::assets

namespace fcg::publish {

namespace {

uint16_t u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }
int16_t i16(const uint8_t* p) { return static_cast<int16_t>(u16(p)); }
uint32_t u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 |
           p[3];
}
void put16(std::string& out, uint32_t v) {
    out += static_cast<char>(v >> 8 & 0xff);
    out += static_cast<char>(v & 0xff);
}
void put32(std::string& out, uint32_t v) {
    put16(out, v >> 16);
    put16(out, v & 0xffff);
}
void set16(std::string& s, std::size_t at, uint32_t v) {
    s[at]     = static_cast<char>(v >> 8 & 0xff);
    s[at + 1] = static_cast<char>(v & 0xff);
}
void set32(std::string& s, std::size_t at, uint32_t v) {
    set16(s, at, v >> 16);
    set16(s, at + 2, v & 0xffff);
}

// A table of the font: where it is in the file.
struct Table {
    uint32_t offset = 0, length = 0;
};

// ---------------------------------------------------------------------------------------------------------------------
// CFF

// An INDEX: its entries' places in the font's data.
struct CffIndex {
    std::size_t start = 0, end = 0;                          // the whole INDEX
    std::vector<std::pair<std::size_t, std::size_t>> items;  // each entry's [begin, end)
};

bool readIndex(std::span<const uint8_t> d, std::size_t at, CffIndex& index) {
    index       = {};
    index.start = at;
    if (at + 2 > d.size()) return false;
    const uint32_t count = u16(&d[at]);
    if (count == 0) {
        index.end = at + 2;
        return true;
    }
    if (at + 3 > d.size()) return false;
    const int off_size = d[at + 2];
    if (off_size < 1 || off_size > 4) return false;
    const std::size_t offsets = at + 3;
    if (offsets + (count + 1) * static_cast<std::size_t>(off_size) > d.size()) return false;
    const auto offset = [&](uint32_t i) {
        uint32_t v = 0;
        for (int k = 0; k < off_size; ++k) v = v << 8 | d[offsets + i * off_size + k];
        return v;
    };
    const std::size_t data = offsets + (count + 1) * static_cast<std::size_t>(off_size) - 1;
    for (uint32_t i = 0; i < count; ++i) {
        const std::size_t b = data + offset(i), e = data + offset(i + 1);
        if (e < b || e > d.size()) return false;
        index.items.push_back({b, e});
    }
    index.end = data + offset(count);
    return index.end <= d.size();
}

// An INDEX written from its entries.
std::string writeIndex(const std::vector<std::string>& items) {
    std::string out;
    put16(out, static_cast<uint32_t>(items.size()));
    if (items.empty()) return out;
    std::size_t total = 1;
    for (const auto& item : items) total += item.size();
    const int off_size = total < 0x100 ? 1 : total < 0x10000 ? 2 : total < 0x1000000 ? 3 : 4;
    out += static_cast<char>(off_size);
    std::size_t at = 1;
    const auto put = [&](std::size_t v) {
        for (int k = off_size - 1; k >= 0; --k) out += static_cast<char>(v >> (8 * k) & 0xff);
    };
    put(at);
    for (const auto& item : items) {
        at += item.size();
        put(at);
    }
    for (const auto& item : items) out += item;
    return out;
}

// A DICT's entry: its operator (12 x as 1200 + x), its operands as numbers, and its bytes.
struct DictEntry {
    int op = 0;
    std::vector<double> operands;
    std::size_t begin = 0, end = 0;  // the operands and the operator
};

std::vector<DictEntry> readDict(std::span<const uint8_t> d, std::size_t begin, std::size_t end) {
    std::vector<DictEntry> out;
    DictEntry e;
    e.begin       = begin;
    std::size_t i = begin;
    while (i < end) {
        const int b0 = d[i];
        if (b0 <= 21) {
            if (b0 == 12) {
                if (i + 1 >= end) break;
                e.op = 1200 + d[i + 1];
                i += 2;
            } else {
                e.op = b0;
                ++i;
            }
            e.end = i;
            out.push_back(e);
            e       = {};
            e.begin = i;
            continue;
        }
        if (b0 == 28 && i + 2 < end) {
            e.operands.push_back(i16(&d[i + 1]));
            i += 3;
        } else if (b0 == 29 && i + 4 < end) {
            e.operands.push_back(static_cast<int32_t>(u32(&d[i + 1])));
            i += 5;
        } else if (b0 == 30) {
            // A real: nibbles until 0xf.
            std::string text;
            ++i;
            bool done = false;
            while (i < end && !done) {
                for (const int nibble : {d[i] >> 4, d[i] & 0xf}) {
                    if (nibble <= 9)
                        text += static_cast<char>('0' + nibble);
                    else if (nibble == 0xa)
                        text += '.';
                    else if (nibble == 0xb)
                        text += 'E';
                    else if (nibble == 0xc)
                        text += "E-";
                    else if (nibble == 0xe)
                        text += '-';
                    else if (nibble == 0xf) {
                        done = true;
                        break;
                    }
                }
                ++i;
            }
            e.operands.push_back(std::strtod(text.c_str(), nullptr));
        } else if (b0 >= 32 && b0 <= 246) {
            e.operands.push_back(b0 - 139);
            ++i;
        } else if (b0 >= 247 && b0 <= 250 && i + 1 < end) {
            e.operands.push_back((b0 - 247) * 256 + d[i + 1] + 108);
            i += 2;
        } else if (b0 >= 251 && b0 <= 254 && i + 1 < end) {
            e.operands.push_back(-(b0 - 251) * 256 - d[i + 1] - 108);
            i += 2;
        } else {
            break;  // not a DICT
        }
    }
    return out;
}

void putInt29(std::string& out, int32_t v) {
    out += static_cast<char>(29);
    put32(out, static_cast<uint32_t>(v));
}

void putOperator(std::string& out, int op) {
    if (op >= 1200) {
        out += static_cast<char>(12);
        out += static_cast<char>(op - 1200);
    } else {
        out += static_cast<char>(op);
    }
}

int subrBias(std::size_t count) { return count < 1240 ? 107 : count < 33900 ? 1131 : 32768; }

// Walks a Type 2 charstring for the subroutines it calls, and those they call: their numbers marked used.
struct CharstringWalk {
    std::span<const uint8_t> d;
    const CffIndex* global         = nullptr;
    const CffIndex* local          = nullptr;
    std::vector<bool>* used_global = nullptr;
    std::vector<bool>* used_local  = nullptr;
    std::vector<double> stack;
    int stems  = 0;
    bool ended = false;

    void run(std::size_t begin, std::size_t end, int depth) {
        if (depth > 10) return;
        std::size_t i = begin;
        while (i < end && !ended) {
            const int b0 = d[i];
            if (b0 >= 32 || b0 == 28) {
                if (b0 == 28 && i + 2 < end) {
                    stack.push_back(i16(&d[i + 1]));
                    i += 3;
                } else if (b0 <= 246) {
                    stack.push_back(b0 - 139);
                    ++i;
                } else if (b0 <= 250 && i + 1 < end) {
                    stack.push_back((b0 - 247) * 256 + d[i + 1] + 108);
                    i += 2;
                } else if (b0 <= 254 && i + 1 < end) {
                    stack.push_back(-(b0 - 251) * 256 - d[i + 1] - 108);
                    i += 2;
                } else if (b0 == 255 && i + 4 < end) {
                    stack.push_back(static_cast<int32_t>(u32(&d[i + 1])) / 65536.0);
                    i += 5;
                } else {
                    return;
                }
                continue;
            }
            ++i;
            switch (b0) {
                case 1:
                case 3:
                case 18:
                case 23:  // stems
                    stems += static_cast<int>(stack.size() / 2);
                    stack.clear();
                    break;
                case 19:
                case 20:  // hintmask, cntrmask: an implied vstem first, then the mask's bytes
                    stems += static_cast<int>(stack.size() / 2);
                    stack.clear();
                    i += static_cast<std::size_t>((stems + 7) / 8);
                    break;
                case 10:    // callsubr
                case 29: {  // callgsubr
                    if (stack.empty()) return;
                    const CffIndex* subrs = b0 == 10 ? local : global;
                    auto* used            = b0 == 10 ? used_local : used_global;
                    if (!subrs || !used) return;
                    const long n = std::lround(stack.back()) + subrBias(subrs->items.size());
                    stack.pop_back();
                    if (n < 0 || n >= static_cast<long>(subrs->items.size())) return;
                    (*used)[static_cast<std::size_t>(n)] = true;
                    run(subrs->items[static_cast<std::size_t>(n)].first,
                        subrs->items[static_cast<std::size_t>(n)].second, depth + 1);
                    break;
                }
                case 11:  // return
                    return;
                case 14:  // endchar
                    ended = true;
                    return;
                case 12:  // two-byte operators: flex and arithmetic; none calls a subroutine
                    ++i;
                    stack.clear();
                    break;
                default:
                    stack.clear();
                    break;
            }
        }
    }
};

// What a CFF is made of, as the subset needs it.
struct Cff {
    std::span<const uint8_t> d;
    int hdr_size = 4;
    CffIndex names, top, strings, gsubrs, charstrings, fdarray;
    std::vector<DictEntry> top_dict;
    std::size_t charset = 0, fdselect = 0;  // offsets; 0 none (charset 0..2 are predefined)
    std::size_t charset_len = 0, fdselect_len = 0;
    bool cid = false;
    struct Private {
        std::size_t begin = 0, end = 0;
        std::vector<DictEntry> dict;
        CffIndex subrs;
        bool has_subrs = false;
    };
    std::vector<Private> privates;              // one (name-keyed), or one per Font DICT of FDArray
    std::vector<std::vector<DictEntry>> fonts;  // FDArray's Font DICTs
    std::vector<uint8_t> fd_of;                 // by glyph
    std::vector<uint16_t> cid_of;               // by glyph, CID-keyed

    const DictEntry* find(const std::vector<DictEntry>& dict, int op) const {
        for (const auto& e : dict)
            if (e.op == op) return &e;
        return nullptr;
    }

    bool readPrivate(const std::vector<DictEntry>& owner, Private& p) const {
        const auto* e = find(owner, 18);
        if (!e || e->operands.size() < 2) return false;
        p.begin = static_cast<std::size_t>(e->operands[1]);
        p.end   = p.begin + static_cast<std::size_t>(e->operands[0]);
        if (p.end > d.size() || p.end < p.begin) return false;
        p.dict = readDict(d, p.begin, p.end);
        if (const auto* s = find(p.dict, 19); s && !s->operands.empty()) {
            p.has_subrs = readIndex(d, p.begin + static_cast<std::size_t>(s->operands[0]), p.subrs);
            if (!p.has_subrs) return false;
        }
        return true;
    }

    bool read(std::span<const uint8_t> data) {
        d = data;
        if (d.size() < 4 || d[0] != 1) return false;
        hdr_size = d[2];
        if (!readIndex(d, static_cast<std::size_t>(hdr_size), names) || !readIndex(d, names.end, top) ||
            top.items.empty() || !readIndex(d, top.end, strings) || !readIndex(d, strings.end, gsubrs))
            return false;
        top_dict       = readDict(d, top.items[0].first, top.items[0].second);
        cid            = find(top_dict, 1230) != nullptr;  // ROS
        const auto* cs = find(top_dict, 17);
        if (!cs || cs->operands.empty() || !readIndex(d, static_cast<std::size_t>(cs->operands[0]), charstrings))
            return false;
        const std::size_t glyphs = charstrings.items.size();
        if (const auto* c = find(top_dict, 15); c && !c->operands.empty() && c->operands[0] > 2) {
            charset = static_cast<std::size_t>(c->operands[0]);
            if (charset >= d.size()) return false;
            // Each glyph's CID (or SID), .notdef's 0 first.
            std::vector<uint16_t> ids(glyphs, 0);
            const int format = d[charset];
            std::size_t at = charset + 1, g = 1;
            if (format == 0) {
                for (; g < glyphs && at + 2 <= d.size(); ++g, at += 2) ids[g] = u16(&d[at]);
            } else if (format == 1 || format == 2) {
                while (g < glyphs && at + (format == 1 ? 3 : 4) <= d.size()) {
                    const uint32_t first = u16(&d[at]);
                    const uint32_t left  = format == 1 ? d[at + 2] : u16(&d[at + 2]);
                    at += format == 1 ? 3 : 4;
                    for (uint32_t k = 0; k <= left && g < glyphs; ++k, ++g) ids[g] = static_cast<uint16_t>(first + k);
                }
            } else {
                return false;
            }
            charset_len = at - charset;
            if (cid) cid_of = std::move(ids);
        } else if (cid) {
            return false;  // a CID-keyed font names its glyphs' CIDs
        }
        if (cid) {
            const auto* fa = find(top_dict, 1236);
            const auto* fs = find(top_dict, 1237);
            if (!fa || !fs || fa->operands.empty() || fs->operands.empty()) return false;
            if (!readIndex(d, static_cast<std::size_t>(fa->operands[0]), fdarray)) return false;
            for (const auto& [b, e] : fdarray.items) {
                fonts.push_back(readDict(d, b, e));
                Private p;
                if (!readPrivate(fonts.back(), p)) return false;
                privates.push_back(std::move(p));
            }
            fdselect = static_cast<std::size_t>(fs->operands[0]);
            if (fdselect >= d.size()) return false;
            fd_of.assign(glyphs, 0);
            const int format = d[fdselect];
            if (format == 0) {
                if (fdselect + 1 + glyphs > d.size()) return false;
                for (std::size_t g = 0; g < glyphs; ++g) fd_of[g] = d[fdselect + 1 + g];
                fdselect_len = 1 + glyphs;
            } else if (format == 3) {
                if (fdselect + 3 > d.size()) return false;
                const uint32_t ranges = u16(&d[fdselect + 1]);
                const std::size_t at  = fdselect + 3;
                if (at + ranges * 3 + 2 > d.size()) return false;
                for (uint32_t r = 0; r < ranges; ++r) {
                    const uint32_t first = u16(&d[at + r * 3]);
                    const uint32_t next  = r + 1 < ranges ? u16(&d[at + (r + 1) * 3]) : u16(&d[at + ranges * 3]);
                    for (uint32_t g = first; g < next && g < glyphs; ++g) fd_of[g] = d[at + r * 3 + 2];
                }
                fdselect_len = 3 + ranges * 3 + 2;
            } else {
                return false;
            }
            for (const auto fd : fd_of)
                if (fd >= privates.size()) return false;
        } else {
            Private p;
            if (find(top_dict, 18)) {
                if (!readPrivate(top_dict, p)) return false;
            }
            privates.push_back(std::move(p));
        }
        return true;
    }

    // The CFF with only `wanted`'s charstrings and the subroutines they call; the rest emptied, every number kept.
    std::string subset(const std::set<int>& wanted) const {
        const std::size_t glyphs = charstrings.items.size();
        std::vector<bool> keep(glyphs, false);
        keep[0] = true;
        for (const int g : wanted)
            if (g >= 0 && static_cast<std::size_t>(g) < glyphs) keep[static_cast<std::size_t>(g)] = true;
        std::vector<bool> used_global(gsubrs.items.size(), false);
        std::vector<std::vector<bool>> used_local;
        for (const auto& p : privates) used_local.emplace_back(p.subrs.items.size(), false);
        for (std::size_t g = 0; g < glyphs; ++g) {
            if (!keep[g]) continue;
            const std::size_t fd = cid ? fd_of[g] : 0;
            CharstringWalk walk;
            walk.d           = d;
            walk.global      = &gsubrs;
            walk.local       = privates[fd].has_subrs ? &privates[fd].subrs : nullptr;
            walk.used_global = &used_global;
            walk.used_local  = &used_local[fd];
            walk.run(charstrings.items[g].first, charstrings.items[g].second, 0);
        }
        const auto entries = [&](const CffIndex& index, const std::vector<bool>& used, char empty) {
            std::vector<std::string> out;
            for (std::size_t i = 0; i < index.items.size(); ++i) {
                if (used[i])
                    out.emplace_back(reinterpret_cast<const char*>(&d[index.items[i].first]),
                                     index.items[i].second - index.items[i].first);
                else
                    out.emplace_back(1, empty);
            }
            return out;
        };
        const std::string gsubr_index = writeIndex(entries(gsubrs, used_global, 11));  // return
        const std::string cs_index    = writeIndex(entries(charstrings, keep, 14));    // endchar
        std::vector<std::string> local_indexes;
        for (std::size_t p = 0; p < privates.size(); ++p)
            local_indexes.push_back(privates[p].has_subrs ? writeIndex(entries(privates[p].subrs, used_local[p], 11))
                                                          : std::string());

        // DICTs written again, their offsets as 5-byte numbers: their sizes do not depend on the offsets.
        const auto rewrite = [&](const std::vector<DictEntry>& dict, const std::map<int, std::vector<int32_t>>& set) {
            std::string out;
            for (const auto& e : dict) {
                if (const auto it = set.find(e.op); it != set.end()) {
                    for (const auto v : it->second) putInt29(out, v);
                    putOperator(out, e.op);
                } else {
                    out.append(reinterpret_cast<const char*>(&d[e.begin]), e.end - e.begin);
                }
            }
            return out;
        };
        const auto has = [&](const std::vector<DictEntry>& dict, int op) { return find(dict, op) != nullptr; };

        // The Private DICTs: their Subrs right after them.
        std::vector<std::string> private_dicts;
        for (const auto& p : privates) {
            std::map<int, std::vector<int32_t>> set;
            if (p.has_subrs) set[19] = {0};
            private_dicts.push_back(rewrite(p.dict, set));
        }
        for (std::size_t p = 0; p < privates.size(); ++p) {
            if (!privates[p].has_subrs) continue;
            std::map<int, std::vector<int32_t>> set;
            set[19]          = {static_cast<int32_t>(private_dicts[p].size())};
            private_dicts[p] = rewrite(privates[p].dict, set);
        }

        // Laid out: header, Name, Top DICT, String, Global Subr, charset, FDSelect, CharStrings, FDArray, Privates.
        std::map<int, std::vector<int32_t>> top_set;
        top_set[17] = {0};
        if (charset) top_set[15] = {0};
        if (cid) {
            top_set[1236] = {0};
            top_set[1237] = {0};
        } else if (has(top_dict, 18)) {
            top_set[18] = {0, 0};
        }
        const std::string names_index   = writeIndex([&] {
            std::vector<std::string> v;
            for (const auto& [b, e] : names.items) v.emplace_back(reinterpret_cast<const char*>(&d[b]), e - b);
            return v;
        }());
        const std::string strings_index = writeIndex([&] {
            std::vector<std::string> v;
            for (const auto& [b, e] : strings.items) v.emplace_back(reinterpret_cast<const char*>(&d[b]), e - b);
            return v;
        }());
        const std::size_t top_size      = writeIndex({rewrite(top_dict, top_set)}).size();
        std::size_t at                  = 4 + names_index.size() + top_size + strings_index.size() + gsubr_index.size();
        const std::size_t charset_at    = at;
        at += charset ? charset_len : 0;
        const std::size_t fdselect_at = at;
        at += cid ? fdselect_len : 0;
        const std::size_t cs_at = at;
        at += cs_index.size();
        // FDArray: its Font DICTs point at their Private DICTs.
        std::vector<std::string> font_dicts;
        for (const auto& f : fonts) font_dicts.push_back(rewrite(f, {{18, {0, 0}}}));
        const std::size_t fdarray_at = at;
        if (cid) at += writeIndex(font_dicts).size();
        std::vector<std::size_t> private_at;
        for (std::size_t p = 0; p < privates.size(); ++p) {
            private_at.push_back(at);
            if (privates[p].end > privates[p].begin || cid) at += private_dicts[p].size() + local_indexes[p].size();
        }
        if (cid) {
            for (std::size_t f = 0; f < fonts.size(); ++f)
                font_dicts[f] = rewrite(
                    fonts[f],
                    {{18, {static_cast<int32_t>(private_dicts[f].size()), static_cast<int32_t>(private_at[f])}}});
            top_set[1236] = {static_cast<int32_t>(fdarray_at)};
            top_set[1237] = {static_cast<int32_t>(fdselect_at)};
        } else if (has(top_dict, 18)) {
            top_set[18] = {static_cast<int32_t>(private_dicts[0].size()), static_cast<int32_t>(private_at[0])};
        }
        top_set[17] = {static_cast<int32_t>(cs_at)};
        if (charset) top_set[15] = {static_cast<int32_t>(charset_at)};

        std::string out;
        out += static_cast<char>(1);
        out += static_cast<char>(0);
        out += static_cast<char>(4);
        out += static_cast<char>(d[3]);
        out += names_index;
        out += writeIndex({rewrite(top_dict, top_set)});
        out += strings_index;
        out += gsubr_index;
        if (charset) out.append(reinterpret_cast<const char*>(&d[charset]), charset_len);
        if (cid) out.append(reinterpret_cast<const char*>(&d[fdselect]), fdselect_len);
        out += cs_index;
        if (cid) out += writeIndex(font_dicts);
        for (std::size_t p = 0; p < privates.size(); ++p) {
            if (!(privates[p].end > privates[p].begin || cid)) continue;
            out += private_dicts[p];
            out += local_indexes[p];
        }
        return out;
    }

    // A string of the String INDEX (SIDs past the 391 standard ones).
    std::string sid(int id) const {
        const int i = id - 391;
        if (i < 0 || static_cast<std::size_t>(i) >= strings.items.size()) return {};
        const auto& [b, e] = strings.items[static_cast<std::size_t>(i)];
        return std::string(reinterpret_cast<const char*>(&d[b]), e - b);
    }
};

// ---------------------------------------------------------------------------------------------------------------------
// TrueType

std::map<std::string, Table> tables(std::span<const uint8_t> d, uint32_t offset) {
    std::map<std::string, Table> out;
    if (offset + 12 > d.size()) return out;
    const uint32_t count = u16(&d[offset + 4]);
    for (uint32_t i = 0; i < count; ++i) {
        const std::size_t r = offset + 12 + i * 16;
        if (r + 16 > d.size()) break;
        Table t{u32(&d[r + 8]), u32(&d[r + 12])};
        if (static_cast<uint64_t>(t.offset) + t.length > d.size()) continue;
        out[std::string(reinterpret_cast<const char*>(&d[r]), 4)] = t;
    }
    return out;
}

uint32_t checksum(std::string_view data) {
    uint32_t sum = 0;
    for (std::size_t i = 0; i < data.size(); i += 4) {
        uint32_t v = 0;
        for (std::size_t k = 0; k < 4; ++k) v = v << 8 | (i + k < data.size() ? static_cast<uint8_t>(data[i + k]) : 0);
        sum += v;
    }
    return sum;
}

std::string sfnt(const std::map<std::string, std::string>& tables_out) {
    const auto count = static_cast<uint32_t>(tables_out.size());
    uint32_t power = 1, log = 0;
    while (power * 2 <= count) {
        power *= 2;
        ++log;
    }
    std::string out;
    put32(out, 0x00010000);
    put16(out, count);
    put16(out, power * 16);
    put16(out, log);
    put16(out, count * 16 - power * 16);
    std::size_t at = 12 + count * 16;
    std::string body;
    std::size_t head_at = 0;
    for (const auto& [tag, data] : tables_out) {
        out += tag;
        put32(out, checksum(data));
        put32(out, static_cast<uint32_t>(at + body.size()));
        put32(out, static_cast<uint32_t>(data.size()));
        if (tag == "head") head_at = at + body.size();
        body += data;
        while (body.size() % 4) body += '\0';
    }
    out += body;
    if (head_at + 12 <= out.size()) set32(out, head_at + 8, 0xB1B0AFBA - checksum(out));
    return out;
}

}  // namespace

struct Font::Impl {
    stbtt_fontinfo info{};
    float scale = 1;  // ems per font unit
    int units   = 1000;
    std::map<std::string, Table> tables;
    std::optional<Cff> cff;
};

Font::Font() : impl_(std::make_unique<Impl>()) {}
Font::~Font() = default;

bool Font::init(std::span<const uint8_t> data, int index) {
    ready_        = false;
    data_         = data;
    const int off = stbtt_GetFontOffsetForIndex(data.data(), index);
    if (off < 0) return false;
    offset_ = static_cast<uint32_t>(off);
    if (!stbtt_InitFont(&impl_->info, data.data(), off)) return false;
    impl_->tables   = tables(data, offset_);
    const auto head = impl_->tables.find("head");
    if (head == impl_->tables.end() || head->second.length < 54) return false;
    const uint8_t* h = &data[head->second.offset];
    impl_->units     = std::max<int>(16, u16(h + 18));
    impl_->scale     = 1.0f / static_cast<float>(impl_->units);
    int ascent = 0, descent = 0, gap = 0;
    stbtt_GetFontVMetrics(&impl_->info, &ascent, &descent, &gap);
    ascent_                = static_cast<float>(ascent) * impl_->scale;
    descent_               = static_cast<float>(descent) * impl_->scale;
    const auto thousandths = [&](int v) { return static_cast<int>(std::lround(v * 1000.0 / impl_->units)); };
    descriptor_.bbox[0]    = thousandths(i16(h + 36));
    descriptor_.bbox[1]    = thousandths(i16(h + 38));
    descriptor_.bbox[2]    = thousandths(i16(h + 40));
    descriptor_.bbox[3]    = thousandths(i16(h + 42));
    descriptor_.ascent     = thousandths(ascent);
    descriptor_.descent    = thousandths(descent);
    descriptor_.cap_height = thousandths(static_cast<int>(ascent * 0.72));
    if (const auto os2 = impl_->tables.find("OS/2"); os2 != impl_->tables.end() && os2->second.length >= 90) {
        const uint8_t* o   = &data[os2->second.offset];
        descriptor_.weight = u16(o + 4);
        if (u16(o) >= 2) descriptor_.cap_height = thousandths(i16(o + 88));
    }
    if (const auto post = impl_->tables.find("post"); post != impl_->tables.end() && post->second.length >= 16) {
        const uint8_t* p         = &data[post->second.offset];
        descriptor_.italic_angle = static_cast<float>(static_cast<int32_t>(u32(p + 4))) / 65536.0f;
        descriptor_.fixed        = u32(p + 12) != 0;
    }
    // Its PostScript name (name 6), for the PDF's BaseFont.
    ps_name_.clear();
    if (const auto name = impl_->tables.find("name"); name != impl_->tables.end() && name->second.length >= 6) {
        const uint8_t* n       = &data[name->second.offset];
        const uint32_t count   = u16(n + 2);
        const uint32_t strings = u16(n + 4);
        for (uint32_t i = 0; i < count && 6 + (i + 1) * 12 <= name->second.length && ps_name_.empty(); ++i) {
            const uint8_t* r        = n + 6 + i * 12;
            const uint32_t platform = u16(r), id = u16(r + 6), len = u16(r + 8), at = u16(r + 10);
            if (id != 6 || strings + at + len > name->second.length) continue;
            const uint8_t* s = n + strings + at;
            for (uint32_t k = platform == 3 ? 1 : 0; k < len; k += platform == 3 ? 2 : 1)
                if (s[k] > 32 && s[k] < 127 && s[k] != '/' && s[k] != '(' && s[k] != ')' && s[k] != '[' &&
                    s[k] != ']' && s[k] != '<' && s[k] != '>' && s[k] != '{' && s[k] != '}' && s[k] != '%')
                    ps_name_ += static_cast<char>(s[k]);
        }
    }
    if (ps_name_.empty()) ps_name_ = "Font";
    // A CFF's outlines: its glyphs' CIDs, and its ROS.
    cff_ = false;
    cid_of_.clear();
    ros_.clear();
    impl_->cff.reset();
    if (const auto cff = impl_->tables.find("CFF "); cff != impl_->tables.end()) {
        cff_ = true;
        Cff c;
        if (c.read(data.subspan(cff->second.offset, cff->second.length))) {
            if (c.cid) {
                cid_of_ = c.cid_of;
                if (const auto* ros = c.find(c.top_dict, 1230); ros && ros->operands.size() >= 3)
                    ros_ = c.sid(static_cast<int>(ros->operands[0])) + " " + c.sid(static_cast<int>(ros->operands[1])) +
                           " " + std::to_string(static_cast<int>(ros->operands[2]));
            }
            impl_->cff = std::move(c);
        }
    }
    ready_ = true;
    return true;
}

int Font::glyph(char32_t c) const { return ready_ ? stbtt_FindGlyphIndex(&impl_->info, static_cast<int>(c)) : 0; }

float Font::advance(int glyph) const {
    if (!ready_) return 0.5f;
    int advance = 0, lsb = 0;
    stbtt_GetGlyphHMetrics(&impl_->info, glyph, &advance, &lsb);
    return static_cast<float>(advance) * impl_->scale;
}

int Font::cid(int glyph) const {
    if (cid_of_.empty()) return glyph;
    return glyph >= 0 && static_cast<std::size_t>(glyph) < cid_of_.size() ? cid_of_[static_cast<std::size_t>(glyph)]
                                                                          : 0;
}

void Font::outline(int glyph, float em, float x, float y, Path& path) const {
    if (!ready_) return;
    stbtt_vertex* v = nullptr;
    const int n     = stbtt_GetGlyphShape(&impl_->info, glyph, &v);
    const float s   = em * impl_->scale;
    float cx = 0, cy = 0;  // the current point, in font units
    bool open = false;
    for (int i = 0; i < n; ++i) {
        const float px = x + v[i].x * s, py = y - v[i].y * s;
        PathCmd c;
        switch (v[i].type) {
            case STBTT_vmove:
                if (open) path.push_back({PathCmd::Close});
                c.op   = PathCmd::Move;
                c.x[0] = px;
                c.y[0] = py;
                open   = true;
                break;
            case STBTT_vline:
                c.op   = PathCmd::Line;
                c.x[0] = px;
                c.y[0] = py;
                break;
            case STBTT_vcurve: {
                // A quadratic curve as the cubic it is.
                const float qx = x + v[i].cx * s, qy = y - v[i].cy * s;
                const float sx = x + cx * s, sy = y - cy * s;
                c.op   = PathCmd::Cubic;
                c.x[0] = sx + 2.0f / 3.0f * (qx - sx);
                c.y[0] = sy + 2.0f / 3.0f * (qy - sy);
                c.x[1] = px + 2.0f / 3.0f * (qx - px);
                c.y[1] = py + 2.0f / 3.0f * (qy - py);
                c.x[2] = px;
                c.y[2] = py;
                break;
            }
            case STBTT_vcubic:
                c.op   = PathCmd::Cubic;
                c.x[0] = x + v[i].cx * s;
                c.y[0] = y - v[i].cy * s;
                c.x[1] = x + v[i].cx1 * s;
                c.y[1] = y - v[i].cy1 * s;
                c.x[2] = px;
                c.y[2] = py;
                break;
            default:
                continue;
        }
        cx = v[i].x;
        cy = v[i].y;
        path.push_back(c);
    }
    if (open) path.push_back({PathCmd::Close});
    if (v) stbtt_FreeShape(&impl_->info, v);
}

std::string Font::subset(const std::set<int>& wanted) const {
    if (!ready_) return {};
    const auto d = data_;
    if (cff_) return impl_->cff ? impl_->cff->subset(wanted) : std::string();

    const auto& t  = impl_->tables;
    const auto get = [&](const char* tag) -> std::optional<std::string_view> {
        const auto it = t.find(tag);
        if (it == t.end()) return std::nullopt;
        return std::string_view(reinterpret_cast<const char*>(&d[it->second.offset]), it->second.length);
    };
    const auto head = get("head"), maxp = get("maxp"), loca = get("loca"), glyf = get("glyf"), hhea = get("hhea"),
               hmtx = get("hmtx");
    if (!head || !maxp || !loca || !glyf || !hhea || !hmtx || head->size() < 54 || maxp->size() < 6) return {};
    const auto* hp         = reinterpret_cast<const uint8_t*>(head->data());
    const bool long_loca   = i16(hp + 50) != 0;
    const uint32_t count   = u16(reinterpret_cast<const uint8_t*>(maxp->data()) + 4);
    const auto* lp         = reinterpret_cast<const uint8_t*>(loca->data());
    const auto* gp         = reinterpret_cast<const uint8_t*>(glyf->data());
    const auto glyph_range = [&](uint32_t g) -> std::pair<uint32_t, uint32_t> {
        if (g >= count) return {0, 0};
        const std::size_t need = (g + 2) * (long_loca ? 4u : 2u);
        if (need > loca->size()) return {0, 0};
        uint32_t b = long_loca ? u32(lp + g * 4) : u16(lp + g * 2) * 2u;
        uint32_t e = long_loca ? u32(lp + (g + 1) * 4) : u16(lp + (g + 1) * 2) * 2u;
        if (e < b || e > glyf->size()) return {0, 0};
        return {b, e};
    };
    // The glyphs wanted and the components of the composite ones among them.
    std::set<uint32_t> keep;
    std::vector<uint32_t> todo{0};
    for (const int g : wanted)
        if (g >= 0) todo.push_back(static_cast<uint32_t>(g));
    while (!todo.empty()) {
        const uint32_t g = todo.back();
        todo.pop_back();
        if (g >= count || !keep.insert(g).second) continue;
        const auto [b, e] = glyph_range(g);
        if (e - b < 10 || i16(gp + b) >= 0) continue;
        std::size_t at = b + 10;
        for (int guard = 0; guard < 64 && at + 4 <= e; ++guard) {
            const uint32_t flags = u16(gp + at), component = u16(gp + at + 2);
            if (!keep.count(component)) todo.push_back(component);
            at += 4 + ((flags & 0x0001) ? 4 : 2);
            if (flags & 0x0008)
                at += 2;
            else if (flags & 0x0040)
                at += 4;
            else if (flags & 0x0080)
                at += 8;
            if (!(flags & 0x0020)) break;
        }
    }
    // glyf and a long loca with only those glyphs.
    std::string new_glyf, new_loca;
    for (uint32_t g = 0; g < count; ++g) {
        put32(new_loca, static_cast<uint32_t>(new_glyf.size()));
        if (!keep.count(g)) continue;
        const auto [b, e] = glyph_range(g);
        new_glyf.append(reinterpret_cast<const char*>(gp + b), e - b);
        while (new_glyf.size() % 4) new_glyf += '\0';
    }
    put32(new_loca, static_cast<uint32_t>(new_glyf.size()));
    std::map<std::string, std::string> out;
    out["glyf"] = std::move(new_glyf);
    out["loca"] = std::move(new_loca);
    std::string new_head(*head);
    set16(new_head, 50, 1);  // a long loca
    set32(new_head, 8, 0);   // checkSumAdjustment, set once the font is written
    out["head"] = std::move(new_head);
    out["maxp"] = std::string(*maxp);
    out["hhea"] = std::string(*hhea);
    out["hmtx"] = std::string(*hmtx);
    for (const char* tag : {"cvt ", "fpgm", "prep", "OS/2", "cmap"})
        if (const auto table = get(tag)) out[tag] = std::string(*table);
    if (const auto post = get("post"); post && post->size() >= 32) {
        std::string p(post->substr(0, 32));
        set32(p, 0, 0x00030000);  // no glyph names
        out["post"] = std::move(p);
    }
    return sfnt(out);
}

// ---------------------------------------------------------------------------------------------------------------------

namespace {

std::mutex g_cjk_mutex;
bool g_cjk_tried = false;
bool g_cjk_ok    = false;
std::vector<uint8_t> g_cjk_data;

}  // namespace

Fonts::Fonts() {
    (void)fonts_[0].init({assets::inter_regular, assets::inter_regular_size}, 0);
    (void)fonts_[1].init({assets::inter_semibold, assets::inter_semibold_size}, 0);
    (void)fonts_[2].init({assets::inter_italic, assets::inter_italic_size}, 0);
}

const Fonts& Fonts::get() {
    static const Fonts fonts;
    return fonts;
}

bool Fonts::cjk() const {
    std::lock_guard<std::mutex> lock(g_cjk_mutex);
    if (!g_cjk_tried) {
        g_cjk_tried = true;
        if (const auto file = ui::cjkFontFile()) {
            std::ifstream in(file->first, std::ios::binary);
            g_cjk_data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            if (!g_cjk_data.empty())
                g_cjk_ok = const_cast<Font&>(fonts_[3]).init({g_cjk_data.data(), g_cjk_data.size()}, file->second);
        }
    }
    return g_cjk_ok;
}

const Font& Fonts::font(FontId id) const {
    const auto i = static_cast<std::size_t>(id);
    if (id == FontId::Cjk && !cjk()) return fonts_[0];
    return fonts_[i];
}

bool wideChar(char32_t c) {
    return (c >= 0x2E80 && c <= 0x9FFF) || (c >= 0xAC00 && c <= 0xD7AF) || (c >= 0xF900 && c <= 0xFAFF) ||
           (c >= 0xFE30 && c <= 0xFE4F) || (c >= 0xFF00 && c <= 0xFFEF) || (c >= 0x20000 && c <= 0x2FA1F);
}

std::u32string utf32(std::string_view s) {
    std::u32string out;
    for (std::size_t i = 0; i < s.size();) {
        const auto b    = static_cast<uint8_t>(s[i]);
        const int extra = b < 0x80 ? 0 : (b >> 5) == 6 ? 1 : (b >> 4) == 14 ? 2 : (b >> 3) == 30 ? 3 : -1;
        if (extra < 0 || i + static_cast<std::size_t>(extra) >= s.size()) {
            out += U'�';
            ++i;
            continue;
        }
        char32_t c = extra == 0 ? b : b & (0x3f >> extra);
        bool bad   = false;
        for (int k = 1; k <= extra; ++k) {
            const auto n = static_cast<uint8_t>(s[i + static_cast<std::size_t>(k)]);
            if ((n & 0xc0) != 0x80) {
                bad = true;
                break;
            }
            c = c << 6 | (n & 0x3f);
        }
        if (bad) {
            out += U'�';
            ++i;
            continue;
        }
        out += c;
        i += static_cast<std::size_t>(extra) + 1;
    }
    return out;
}

std::string utf8(std::u32string_view text) {
    std::string out;
    for (const char32_t c : text) {
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xc0 | c >> 6);
            out += static_cast<char>(0x80 | (c & 0x3f));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xe0 | c >> 12);
            out += static_cast<char>(0x80 | (c >> 6 & 0x3f));
            out += static_cast<char>(0x80 | (c & 0x3f));
        } else {
            out += static_cast<char>(0xf0 | c >> 18);
            out += static_cast<char>(0x80 | (c >> 12 & 0x3f));
            out += static_cast<char>(0x80 | (c >> 6 & 0x3f));
            out += static_cast<char>(0x80 | (c & 0x3f));
        }
    }
    return out;
}

std::vector<GlyphRun> shapeText(std::string_view text, FontId font) {
    const auto& fonts = Fonts::get();
    std::vector<GlyphRun> runs;
    for (char32_t c : utf32(text)) {
        if (c == U'\t' || c == U'\n' || c == U'\r') c = U' ';
        FontId use = font;
        int g      = fonts.font(font).glyph(c);
        if (g == 0 && c > 0x7f && fonts.cjk()) {
            if (const int k = fonts.font(FontId::Cjk).glyph(c); k != 0) {
                use = FontId::Cjk;
                g   = k;
            }
        }
        if (runs.empty() || runs.back().font != use) {
            runs.emplace_back();
            runs.back().font = use;
        }
        auto& run = runs.back();
        run.glyphs.push_back(g);
        run.chars.push_back(c);
        run.width += fonts.font(use).advance(g);
    }
    return runs;
}

float textWidth(std::string_view text, FontId font, float em) {
    float w = 0;
    for (const auto& run : shapeText(text, font)) w += run.width;
    return w * em;
}

float textOutline(std::string_view text, FontId font, float em, float x, float y, Path& path) {
    const auto& fonts = Fonts::get();
    float at          = x;
    for (const auto& run : shapeText(text, font)) {
        const auto& f = fonts.font(run.font);
        for (const int g : run.glyphs) {
            f.outline(g, em, at, y, path);
            at += f.advance(g) * em;
        }
    }
    return at - x;
}

}  // namespace fcg::publish
