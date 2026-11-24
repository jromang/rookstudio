#include "publish_html.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <map>

#include <fmt/include/fmt/format.h>

#include <study/game_tree.hpp>

#include "board_page.hpp"
#include "board_text.hpp"
#include "board_view.hpp"
#include "i18n.hpp"
#include "publish_text.hpp"
#include "ui.hpp"

namespace fcg::publish {

namespace {

std::string colourHex(Rgba c) { return fmt::format("#{:02x}{:02x}{:02x}", c.r, c.g, c.b); }

std::string opacity(Rgba c) {
    auto s = fmt::format("{:.2f}", c.a / 255.0);
    while (s.back() == '0') s.pop_back();
    if (s.back() == '.') s.pop_back();
    return s;
}

// A string as JSON, safe inside a <script>: no "<", ">" or "&" as such.
std::string json(const std::string& s) {
    std::string out = "\"";
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                break;
            case '\t':
                out += "\\t";
                break;
            case '<':
                out += "\\u003c";
                break;
            case '>':
                out += "\\u003e";
                break;
            case '&':
                out += "\\u0026";
                break;
            default:
                if (c < 0x20)
                    out += fmt::format("\\u{:04x}", c);
                else
                    out += ch;
        }
    }
    return out + "\"";
}

std::string num1(float v) {
    auto s = fmt::format("{:.1f}", v);
    if (s.size() > 2 && s.substr(s.size() - 2) == ".0") s.resize(s.size() - 2);
    return s;
}

// A move's marks as the page's data: its squares ("q") and arrows' polygons ("r"), in a board 800 wide, White below;
// `suffix` after their names ("q0" for the start's).
std::string marksData(const MoveNotes& notes, const std::string& suffix) {
    std::string out;
    if (!notes.squares.empty()) {
        out += ",q" + suffix + ":[";
        bool first = true;
        for (const auto& m : notes.squares) {
            if (m.from < 0 || m.from > 63) continue;
            const auto c = rgba(markColour(m.colour, true));
            out += fmt::format("{}[{},\"{}\",{}]", first ? "" : ",", m.from, colourHex(c), opacity(c));
            first = false;
        }
        out += "]";
    }
    if (!notes.arrows.empty()) {
        out += ",r" + suffix + ":[";
        bool first = true;
        for (const auto& m : notes.arrows) {
            if (m.from < 0 || m.to < 0 || m.from > 63 || m.to > 63 || m.from == m.to) continue;
            const auto c      = rgba(markColour(m.colour, false));
            const auto centre = [](int sq) {
                return std::array<float, 2>{(sq % 8) * 100.0f + 50.0f, (7 - sq / 8) * 100.0f + 50.0f};
            };
            const auto a = centre(m.from), b = centre(m.to);
            for (const auto& polygon : arrowShapes(a[0], a[1], b[0], b[1], 100.0f)) {
                std::string points;
                for (const auto& p : polygon) points += (points.empty() ? "" : " ") + num1(p[0]) + "," + num1(p[1]);
                out += fmt::format("{}[\"{}\",\"{}\",{}]", first ? "" : ",", points, colourHex(c), opacity(c));
                first = false;
            }
        }
        out += "]";
    }
    return out;
}

std::string placement(const chess::Board& board) {
    const auto fen = board.getFen();
    return fen.substr(0, fen.find(' '));
}

// The nodes of a game's tree: its moves, each after its parent (-1 the start), a line's own moves before the
// variations that leave it, so that a node's first child carries its line on.
struct Node {
    int parent             = -1;
    int line               = -1;  // the tree's line it is a move of, and its place there (its full line's ply)
    int ply                = 0;
    const ReplayMove* move = nullptr;
};

struct Nodes {
    std::vector<Node> nodes;
    std::map<std::pair<int, int>, int> at;  // (line, ply) -> node, ply from 1
};

Nodes nodesOf(const GameTree& tree) {
    Nodes n;
    const std::function<int(int, int)> nodeAt = [&](int line, int ply) -> int {
        if (ply <= 0) return -1;
        if (const auto it = n.at.find({line, ply}); it != n.at.end()) return it->second;
        if (line >= 0) {
            const auto& v = tree.variations[static_cast<std::size_t>(line)];
            if (ply <= v.from) return nodeAt(v.parent, ply);
        }
        return -1;
    };
    for (std::size_t i = 0; i < tree.main.moves.size(); ++i) {
        n.nodes.push_back({static_cast<int>(i) - 1, -1, static_cast<int>(i) + 1, &tree.main.moves[i]});
        n.at[{-1, static_cast<int>(i) + 1}] = static_cast<int>(i);
    }
    // The variations once their parent's nodes are there.
    std::vector<bool> done(tree.variations.size(), false);
    for (bool progress = true; progress;) {
        progress = false;
        for (std::size_t v = 0; v < tree.variations.size(); ++v) {
            const auto& var = tree.variations[v];
            if (done[v] || (var.parent >= 0 && !done[static_cast<std::size_t>(var.parent)])) continue;
            done[v]    = true;
            progress   = true;
            int parent = nodeAt(var.parent, var.from);
            for (std::size_t k = 0; k < var.moves.size(); ++k) {
                const int ply = var.from + static_cast<int>(k) + 1;
                n.nodes.push_back({parent, static_cast<int>(v), ply, &var.moves[k]});
                parent                           = static_cast<int>(n.nodes.size()) - 1;
                n.at[{static_cast<int>(v), ply}] = parent;
            }
        }
    }
    return n;
}

}  // namespace

std::string resultText(const std::string& result) {
    if (result == "1/2-1/2") return "½-½";
    if (result == "1-0" || result == "0-1") return result;
    return "*";
}

std::string htmlText(const std::string& text) {
    std::string out;
    for (const char c : text) {
        if (c == '&')
            out += "&amp;";
        else if (c == '<')
            out += "&lt;";
        else if (c == '>')
            out += "&gt;";
        else if (c == '"')
            out += "&quot;";
        else
            out += c;
    }
    return out;
}

std::string gameTitle(const HtmlGame& game) {
    const auto& m     = game.tree.main;
    std::string white = game.white.empty() ? m.header("White") : game.white;
    std::string black = game.black.empty() ? m.header("Black") : game.black;
    if (white.empty() || white == "?") white = "?";
    if (black.empty() || black == "?") black = "?";
    return trf("{} – {} · {}", white, black, resultText(m.result.empty() ? m.header("Result") : m.result));
}

std::string gameSubtitle(const HtmlGame& game) {
    const auto& m = game.tree.main;
    std::vector<std::string> parts;
    const auto known = [](const std::string& v) { return !v.empty() && v != "?" && v.find("??") == std::string::npos; };
    std::string event = m.header("Event"), site = m.header("Site");
    if (known(event) && known(site))
        parts.push_back(trf("{}, {}", event, site));
    else if (known(event))
        parts.push_back(event);
    else if (known(site))
        parts.push_back(site);
    if (const auto round = m.header("Round"); known(round) && round != "-") parts.push_back(trf("round {}", round));
    if (const auto date = m.header("Date"); !date.empty() && date.substr(0, 4) != "????") parts.push_back(date);
    std::string eco     = m.header("ECO");
    std::string opening = m.header("Opening");
    if (const auto variation = m.header("Variation"); !variation.empty() && !opening.empty())
        opening = trf("{}, {}", opening, variation);
    if (known(eco) && !opening.empty())
        parts.push_back(eco + " " + opening);
    else if (known(eco))
        parts.push_back(eco);
    else if (!opening.empty())
        parts.push_back(opening);
    std::string out;
    for (const auto& p : parts) out += (out.empty() ? "" : " · ") + p;
    return out;
}

std::string pageStart(const std::string& title, const std::string& lang, const std::string& styles) {
    return "<!doctype html>\n<html lang=\"" + htmlText(lang) +
           "\">\n<head>\n<meta charset=\"utf-8\">\n<meta name=\"viewport\" content=\"width=device-width, "
           "initial-scale=1\">\n<meta name=\"generator\" content=\"RookStudio\">\n<title>" +
           htmlText(title) + "</title>\n<style>\n" + styles + "</style>\n</head>\n<body>\n";
}

std::string boardStyles(const appearance::BoardColours& colours) {
    const auto light = rgba(colours.light), dark = rgba(colours.dark), high = rgba(colours.highlight);
    std::string css =
        R"CSS(:root{--text:#1f2329;--muted:#5d6570;--line:#e0e3e8;--accent:#2f6fde;--hover:#eef2fb;--on:#dfe9fb}
*{box-sizing:border-box}
body{margin:0;background:#f4f5f7;color:var(--text);font:15px/1.55 system-ui,-apple-system,"Segoe UI",Roboto,"Noto Sans",sans-serif}
header,main,footer{max-width:1180px;margin:0 auto;padding:16px}
h1{font-size:1.35em;margin:0 0 2px}
h2{font-size:1.1em;margin:22px 0 8px}
h3{font-size:1em;margin:16px 0 6px}
.sub{color:var(--muted)}
.games{display:flex;gap:6px;align-items:center;margin-top:10px;flex-wrap:wrap}
.games select{flex:1 1 260px;min-width:0;font:inherit;padding:5px;border:1px solid var(--line);border-radius:6px;background:#fff}
main.game{display:flex;flex-wrap:wrap;gap:24px;align-items:flex-start}
.boardcol{flex:0 1 480px;min-width:260px;width:100%}
.movescol{flex:1 1 320px;min-width:0}
#board{width:100%;height:auto;display:block;border-radius:2px;user-select:none}
.nav{display:flex;gap:6px;justify-content:center;margin:8px 0}
.nav button,.games button{font:inherit;min-width:42px;padding:5px 10px;border:1px solid var(--line);border-radius:7px;background:#fff;cursor:pointer}
.nav button:hover,.games button:hover{background:var(--hover)}
#curve{width:100%;height:80px;display:block;background:#fff;border:1px solid var(--line);border-radius:6px;cursor:pointer}
.moves{background:#fff;border:1px solid var(--line);border-radius:10px;padding:12px 14px;max-height:72vh;overflow:auto}
.mv{cursor:pointer;padding:0 2px;border-radius:3px;white-space:nowrap}
.mv:hover{background:var(--hover)}
.mv.on{background:var(--on);font-weight:600}
.num{color:var(--muted)}
.nag{color:#b25d10}
.cm{color:#2e7d4f}
.var{color:var(--muted)}
.as{color:#54688a;font-style:italic}
.asl{font-style:normal;font-size:.78em;font-weight:600;border:1px solid #9fb0cc;border-radius:4px;padding:0 4px;margin-right:4px}
.ev,.clk{color:#7a828e;font-size:.82em}
.res{font-weight:600}
.hint{color:var(--muted);font-size:.88em}
footer{color:var(--muted);font-size:.85em}
footer pre{white-space:pre-wrap;font-size:.92em}
table{border-collapse:collapse;font-variant-numeric:tabular-nums;background:#fff}
th,td{padding:4px 8px;border-bottom:1px solid #eef0f3;text-align:left;vertical-align:top}
th{color:var(--muted);font-weight:600;font-size:.9em}
td.n,th.n{text-align:right}
td.c,th.c{text-align:center}
.card{background:#fff;border:1px solid var(--line);border-radius:10px;padding:12px 14px;margin:10px 0}
.wrap{overflow-x:auto}
.bar{display:inline-block;height:10px;background:var(--accent);border-radius:2px;vertical-align:middle}
.doc{display:flex;flex-wrap:wrap;gap:24px;align-items:flex-start}
.doc .text{flex:1 1 560px;min-width:0}
.doc .side{flex:0 1 360px;min-width:240px;position:sticky;top:8px}
.diagram{display:inline-block;margin:6px 12px 6px 0;text-align:center;color:var(--muted);font-size:.88em}
.diagram svg{width:220px;height:auto;display:block}
)CSS";
    css += fmt::format(".l{{fill:{}}}.d{{fill:{}}}.h{{fill:{};fill-opacity:{}}}.cl{{fill:{}}}.cd{{fill:{}}}\n",
                       colourHex(light), colourHex(dark), colourHex(high), opacity(high), colourHex(dark),
                       colourHex(light));
    css += ".co{font:600 19px system-ui,sans-serif}\n";
    return css;
}

std::string pieceSymbols(const PieceShapes& pieces) {
    std::string out = "<svg width=\"0\" height=\"0\" style=\"position:absolute\" aria-hidden=\"true\"><defs>";
    for (std::size_t i = 0; i < 12; ++i) {
        const std::string id = std::string(appearance::PIECE_FILES[i]);
        out += "<symbol id=\"" + id + "\" viewBox=\"0 0 100 100\">" +
               svgElements(pieces.pieces[i], id + "g", 0, 0, 100) + "</symbol>";
    }
    return out + "</defs></svg>\n";
}

std::string boardWidget(bool curve) {
    std::string out = "<svg id=\"board\" viewBox=\"0 0 800 800\" role=\"img\" aria-label=\"" +
                      htmlText(TR("The board")) + "\"></svg>\n<div class=\"nav\">";
    const std::pair<const char*, std::string> buttons[] = {
        {"first", "|‹"}, {"prev", "‹"}, {"next", "›"}, {"last", "›|"}, {"flip", "⇅"}};
    const std::string tips[] = {TR("The start (Home)"), TR("The move before (←)"), TR("The next move (→)"),
                                TR("The end of the line (End)"), TR("Turn the board over (F)")};
    for (std::size_t i = 0; i < 5; ++i)
        out += fmt::format("<button type=\"button\" data-go=\"{}\" title=\"{}\" aria-label=\"{}\">{}</button>",
                           buttons[i].first, htmlText(tips[i]), htmlText(tips[i]), buttons[i].second);
    out += "</div>\n";
    if (curve) out += "<svg id=\"curve\" viewBox=\"0 0 480 80\" preserveAspectRatio=\"none\" hidden></svg>\n";
    return out;
}

std::string boardScript() {
    return R"JS(
(function(){
"use strict";
var G=0,N=-1,flipped=false;
var board=document.getElementById("board"),curve=document.getElementById("curve");
var KIDS=GAMES.map(function(g){var k=[[]];for(var i=0;i<g.n.length;i++)k.push([]);g.n.forEach(function(n,i){k[n.p+1].push(i)});return k});
function squares(fen){var out=[],rows=fen.split("/");for(var r=0;r<8;r++){var f=0;for(var i=0;i<rows[r].length;i++){var c=rows[r][i];if(c>="1"&&c<="8")f+=+c;else{out[(7-r)*8+f]=c;f++}}}return out}
function xy(sq){var file=sq%8,rank=sq>>3;return[(flipped?7-file:file)*100,(flipped?rank:7-rank)*100]}
function rect(sq,attrs){var p=xy(sq);return'<rect x="'+p[0]+'" y="'+p[1]+'" width="100" height="100" '+attrs+"/>"}
function draw(){
 if(!board)return;
 var g=GAMES[G],n=N<0?null:g.n[N],s="",i,p;
 for(i=0;i<64;i++)s+=rect(i,'class="'+((i%8+(i>>3))%2?"l":"d")+'"');
 if(n&&n.m)for(i=0;i<2;i++)s+=rect(n.m[i],'class="h"');
 var q=n?n.q:g.q0,r=n?n.r:g.r0,k=n?n.k:g.k0;
 if(MARKS&&q)q.forEach(function(m){s+=rect(m[0],'fill="'+m[1]+'" fill-opacity="'+m[2]+'"')});
 if(k!==undefined){p=xy(k);s+='<circle cx="'+(p[0]+50)+'" cy="'+(p[1]+50)+'" r="48" fill="#dc2828" fill-opacity="0.59"/>'}
 for(i=0;i<8;i++){
  var fsq=flipped?(7-i)+56:i,rsq=flipped?(7-i)*8+7:i*8;p=xy(fsq);
  s+='<text class="co '+((fsq%8+(fsq>>3))%2?"cl":"cd")+'" x="'+(p[0]+86)+'" y="'+(p[1]+94)+'">'+"abcdefgh"[fsq%8]+"</text>";
  p=xy(rsq);s+='<text class="co '+((rsq%8+(rsq>>3))%2?"cl":"cd")+'" x="'+(p[0]+4)+'" y="'+(p[1]+19)+'">'+((rsq>>3)+1)+"</text>"}
 var P=squares(n?n.f:g.f0);
 for(i=0;i<64;i++)if(P[i]){var c=P[i],id=(c===c.toUpperCase()?"w":"b")+c.toUpperCase();p=xy(i);s+='<use href="#'+id+'" x="'+p[0]+'" y="'+p[1]+'" width="100" height="100"/>'}
 if(MARKS&&r){s+="<g"+(flipped?' transform="rotate(180 400 400)"':"")+">";r.forEach(function(a){s+='<polygon points="'+a[0]+'" fill="'+a[1]+'" fill-opacity="'+a[2]+'"/>'});s+="</g>"}
 board.innerHTML=s;
 document.querySelectorAll(".mv.on").forEach(function(e){e.classList.remove("on")});
 if(n){var e=document.getElementById("m"+G+"-"+N);if(e){e.classList.add("on");var box=e.closest(".moves");if(box){var a=e.getBoundingClientRect(),b=box.getBoundingClientRect();if(a.top<b.top||a.bottom>b.bottom)box.scrollTop+=a.top-b.top-b.height/2}}}
 drawCurve();
}
function drawCurve(){
 if(!curve)return;var e=GAMES[G].e;
 if(!e||e.length<2){curve.hidden=true;return}curve.hidden=false;
 var pts=e.map(function(v,i){return(i*480/(e.length-1)).toFixed(1)+","+(40-Math.max(-5,Math.min(5,v[1]))*7.6).toFixed(1)});
 var s='<rect width="480" height="80" fill="#fff"/><polygon points="0,40 '+pts.join(" ")+' 480,40" fill="#cfd6e2"/><line x1="0" y1="40" x2="480" y2="40" stroke="#9aa3ae" stroke-width="1"/><polyline points="'+pts.join(" ")+'" fill="none" stroke="#2f6fde" stroke-width="1.6"/>';
 for(var i=0;i<e.length;i++)if(e[i][0]===N){var x=i*480/(e.length-1);s+='<line x1="'+x+'" y1="0" x2="'+x+'" y2="80" stroke="#1f2329" stroke-width="1.2"/>'}
 curve.innerHTML=s;
}
function go(n){N=n;draw()}
function kids(n){return KIDS[G][n+1]||[]}
function next(){var k=kids(N);if(k.length)go(k[0])}
function prev(){if(N>=0)go(GAMES[G].n[N].p)}
function last(){var n=N;for(var k=kids(n);k.length;k=kids(n))n=k[0];go(n)}
function choose(g,shown){
 G=g;N=GAMES[g].s;
 var moves=document.getElementById("moves");
 if(shown&&moves){moves.innerHTML=GAMES[g].t;document.getElementById("title").textContent=GAMES[g].h;document.getElementById("sub").textContent=GAMES[g].u;var sel=document.getElementById("games");if(sel)sel.value=g}
 draw();
}
document.addEventListener("click",function(ev){
 var t=ev.target.closest(".mv");if(t){if(+t.dataset.g!==G)choose(+t.dataset.g,false);go(+t.dataset.n);return}
 var b=ev.target.closest("[data-go]");if(!b)return;var w=b.dataset.go;
 if(w==="first")go(-1);else if(w==="prev")prev();else if(w==="next")next();else if(w==="last")last();
 else if(w==="flip"){flipped=!flipped;draw()}
 else if(w==="game-"&&G>0)choose(G-1,true);else if(w==="game+"&&G<GAMES.length-1)choose(G+1,true);
});
if(curve)curve.addEventListener("click",function(ev){var e=GAMES[G].e;if(!e||e.length<2)return;var b=curve.getBoundingClientRect();var i=Math.round((ev.clientX-b.left)/b.width*(e.length-1));go(e[Math.max(0,Math.min(e.length-1,i))][0])});
var sel=document.getElementById("games");if(sel)sel.addEventListener("change",function(){choose(+sel.value,true)});
document.addEventListener("keydown",function(ev){
 var t=ev.target.tagName;if(t==="SELECT"||t==="INPUT"||t==="TEXTAREA"||ev.ctrlKey||ev.metaKey||ev.altKey)return;
 var k=ev.key;
 if(k==="ArrowRight")next();else if(k==="ArrowLeft")prev();else if(k==="Home")go(-1);else if(k==="End")last();
 else if(k==="f"||k==="F"){flipped=!flipped;draw()}
 else if(k==="PageDown"&&G<GAMES.length-1)choose(G+1,true);else if(k==="PageUp"&&G>0)choose(G-1,true);
 else return;
 ev.preventDefault();
});
choose(0,!!document.getElementById("moves"));
var at=/^#(\d+)-(-?\d+)$/.exec(location.hash);
if(at&&+at[1]<GAMES.length){if(+at[1]!==G)choose(+at[1],!!document.getElementById("moves"));if(+at[2]<GAMES[G].n.length)go(+at[2])}
})();
)JS";
}

std::string gameData(const HtmlGame& game, const HtmlOptions& options, int id) {
    (void)id;
    const auto& tree = game.tree;
    const auto nodes = nodesOf(tree);
    const auto start = boardAt(tree.main, game.chess960, 0);
    std::string out  = "{";
    out += "h:" + json(gameTitle(game)) + ",u:" + json(gameSubtitle(game));
    out += ",f0:" + json(placement(start));
    if (start.inCheck()) out += fmt::format(",k0:{}", start.kingSq(start.sideToMove()).index());
    if (options.marks) out += marksData(game.start_notes, "0");
    // The position the page opens at.
    int opening = -1;
    if (game.ply > 0) {
        const auto it = nodes.at.find({game.line, game.ply});
        if (it != nodes.at.end()) opening = it->second;
        if (it == nodes.at.end() && game.line >= 0) {
            const auto& v = tree.variations[static_cast<std::size_t>(game.line)];
            if (game.ply <= v.from)
                if (const auto p = nodes.at.find({-1, game.ply}); p != nodes.at.end()) opening = p->second;
        }
    }
    out += fmt::format(",s:{}", opening);
    out += ",n:[";
    // Each node's position: replayed along its line.
    std::map<int, chess::Board> boards;  // node -> board after it
    std::string curve;
    for (std::size_t i = 0; i < nodes.nodes.size(); ++i) {
        const auto& node = nodes.nodes[i];
        chess::Board b   = node.parent < 0 ? start : boards.at(node.parent);
        const auto& m    = *node.move;
        b.makeMove<true>(m.move);
        boards[static_cast<int>(i)] = b;
        out += i == 0 ? "{" : ",{";
        out += fmt::format("p:{},f:{}", node.parent, json(placement(b)));
        out += fmt::format(",m:[{},{}]", m.move.from().index(), displayedTarget(m.move).index());
        if (b.inCheck()) out += fmt::format(",k:{}", b.kingSq(b.sideToMove()).index());
        if (options.marks) out += marksData(m.notes, "");
        out += "}";
        if (node.line < 0 && m.score && options.evals) {
            const double pawns = ui::whitePawns(m.score, m.mover, 10.0f);
            curve += fmt::format("{}[{},{:.2f}]", curve.empty() ? "" : ",", i, pawns);
        }
    }
    out += "]";
    if (!curve.empty()) out += ",e:[" + curve + "]";
    out += "}";
    return out;
}

std::string movetextHtml(const HtmlGame& game, const HtmlOptions& options, int id) {
    const auto& tree = game.tree;
    const auto nodes = nodesOf(tree);
    // The variations leaving each line, by the ply of the move they take the place of.
    std::map<std::pair<int, int>, std::vector<int>> leaving;
    if (options.variations)
        for (std::size_t v = 0; v < tree.variations.size(); ++v)
            leaving[{tree.variations[v].parent, tree.variations[v].from + 1}].push_back(static_cast<int>(v));
    const auto start       = boardAt(tree.main, game.chess960, 0);
    const int start_number = start.fullMoveNumber();
    const bool black_first = start.sideToMove() == chess::Color::BLACK;
    const auto number      = [&](int ply) {  // the move number of the move at `ply` (from 1)
        const int half = ply - 1 + (black_first ? 1 : 0);
        return start_number + half / 2;
    };
    const auto white_moves = [&](int ply) { return (ply - 1 + (black_first ? 1 : 0)) % 2 == 0; };

    std::string out;
    const auto comment = [&](const std::string& text, const char* cls) {
        if (text.empty()) return;
        out += std::string("<span class=\"") + cls + "\">" + htmlText(text) + "</span> ";
    };
    const std::function<void(int, int)> line = [&](int which, int first_ply) {
        bool need_number  = true;
        const auto& moves = which < 0 ? tree.main.moves : tree.variations[static_cast<std::size_t>(which)].moves;
        const int base    = which < 0 ? 0 : tree.variations[static_cast<std::size_t>(which)].from;
        for (int k = 0; k < static_cast<int>(moves.size()); ++k) {
            const int ply = base + k + 1;
            if (ply < first_ply) continue;
            const auto& m = moves[static_cast<std::size_t>(k)];
            const auto it = nodes.at.find({which, ply});
            if (it == nodes.at.end()) continue;
            if (options.comments && !m.notes.before.empty()) {
                comment(m.notes.before, "cm");
                need_number = true;
            }
            if (white_moves(ply))
                out += fmt::format("<span class=\"num\">{}.</span>", number(ply));
            else if (need_number)
                out += fmt::format("<span class=\"num\">{}…</span>", number(ply));
            need_number = false;
            out += fmt::format("<span class=\"mv\" id=\"m{}-{}\" data-g=\"{}\" data-n=\"{}\">{}", id, it->second, id,
                               it->second, htmlText(localSan(m.san)));
            for (const auto nag : m.notes.nags)
                if (nag <= 9) out += "<span class=\"nag\">" + htmlText(nagSign(nag)) + "</span>";
            out += "</span>";
            for (const auto nag : m.notes.nags)
                if (nag > 9) out += " <span class=\"nag\">" + htmlText(nagSign(nag)) + "</span>";
            if (options.evals && m.score)
                out += " <span class=\"ev\">" + htmlText(evalText(*m.score, m.mover)) + "</span>";
            if (options.clocks && m.clock_ms)
                out += " <span class=\"clk\">" + htmlText(clockText(*m.clock_ms)) + "</span>";
            out += " ";
            if (options.comments && !m.notes.after.empty()) {
                comment(m.notes.after, "cm");
                need_number = true;
            }
            if (options.assistant && !m.notes.assistant.empty()) {
                out += "<span class=\"as\" title=\"" + htmlText(TR("Written by RookStudio's assistant")) +
                       "\"><span class=\"asl\">" + htmlText(TR("Assistant")) + "</span>" + htmlText(m.notes.assistant) +
                       "</span> ";
                need_number = true;
            }
            if (const auto v = leaving.find({which, ply}); v != leaving.end()) {
                for (const int var : v->second) {
                    out += "<span class=\"var\">( ";
                    line(var, 0);
                    out += ")</span> ";
                }
                need_number = true;
            }
        }
    };
    if (options.comments && !tree.main.pre_comment.empty()) comment(tree.main.pre_comment, "cm");
    line(-1, 1);
    out += "<span class=\"res\">" + htmlText(resultText(tree.main.result)) + "</span>";
    return out;
}

std::string noticeHtml(const HtmlLook& look) {
    std::string out = "<footer>";
    if (!look.pieces_notice.empty()) out += "<p>" + htmlText(look.pieces_notice) + "</p>";
    if (!look.pieces_license.empty())
        out += "<details><summary>" + htmlText(TR("The pieces' license")) + "</summary><pre>" +
               htmlText(look.pieces_license) + "</pre></details>";
    out += "<p>" + htmlText(TR("Made with RookStudio.")) + "</p></footer>\n";
    return out;
}

std::string gamesHtml(const std::vector<HtmlGame>& games, const HtmlOptions& options, const HtmlLook& look,
                      const std::string& title) {
    std::string out = pageStart(title, look.lang, boardStyles(look.colours));
    out += "<header><h1 id=\"title\"></h1><div id=\"sub\" class=\"sub\"></div>";
    if (games.size() > 1) {
        out += "<div class=\"games\"><button type=\"button\" data-go=\"game-\" title=\"" +
               htmlText(TR("The game before (Page Up)")) + "\">‹</button><select id=\"games\" aria-label=\"" +
               htmlText(TR("The game shown")) + "\">";
        for (std::size_t i = 0; i < games.size(); ++i)
            out += fmt::format("<option value=\"{}\">{}. {}</option>", i, i + 1, htmlText(gameTitle(games[i])));
        out += "</select><button type=\"button\" data-go=\"game+\" title=\"" +
               htmlText(TR("The next game (Page Down)")) + "\">›</button></div>";
    }
    out += "</header>\n<main class=\"game\"><section class=\"boardcol\">" + boardWidget(options.evals) +
           "</section>\n<section class=\"movescol\"><div id=\"moves\" class=\"moves\"></div><p class=\"hint\">" +
           htmlText(TR("A click on a move shows it, in a variation too; ← and → go through the moves, F turns the "
                       "board over.")) +
           "</p></section></main>\n";
    out += noticeHtml(look);
    if (look.pieces) out += pieceSymbols(*look.pieces);
    out += "<script>\nvar MARKS=" + std::string(options.marks ? "true" : "false") + ";\nvar GAMES=[";
    for (std::size_t i = 0; i < games.size(); ++i) {
        auto data = gameData(games[i], options, static_cast<int>(i));
        data.pop_back();  // its movetext too
        out += (i ? ",\n" : "") + data + ",t:" + json(movetextHtml(games[i], options, static_cast<int>(i))) + "}";
    }
    out += "];\n" + boardScript() + "</script>\n</body>\n</html>\n";
    return out;
}

std::string gamesIndexHtml(const std::vector<IndexedPage>& pages, const HtmlLook& look, const std::string& title) {
    std::string out = pageStart(title, look.lang, boardStyles(look.colours));
    out += "<header><h1>" + htmlText(title) + "</h1><div class=\"sub\">" +
           htmlText(trn("{} game, a page each", "{} games, a page each", pages.size(), pages.size())) +
           "</div></header>\n<main><table>";
    for (std::size_t i = 0; i < pages.size(); ++i)
        out += fmt::format("<tr><td class=\"n\">{}</td><td><a href=\"{}\">{}</a></td><td>{}</td></tr>", i + 1,
                           htmlText(pages[i].file), htmlText(pages[i].title), htmlText(pages[i].subtitle));
    out += "</table></main>\n" + noticeHtml(look) + "</body>\n</html>\n";
    return out;
}

std::string writeGamePages(const std::filesystem::path& folder, std::size_t count,
                           const std::function<std::optional<HtmlGame>(std::size_t)>& game, const HtmlOptions& options,
                           const HtmlLook& look, const std::string& title, const std::atomic<bool>* cancel,
                           std::atomic<int>* done) {
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    const auto write = [](const std::filesystem::path& path, const std::string& bytes) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << bytes;
        return static_cast<bool>(out);
    };
    const auto text = [](const std::filesystem::path& path) {
        const auto u8 = path.u8string();
        return std::string(u8.begin(), u8.end());
    };
    std::vector<IndexedPage> pages;
    for (std::size_t i = 0; i < count; ++i) {
        if (cancel && *cancel) return TR("Cancelled.");
        const auto g = game(i);
        if (done) ++*done;
        if (!g) continue;
        auto stem = fileStem(gameTitle(*g));
        if (stem.empty()) stem = "game";
        IndexedPage page{fmt::format("{:04}-{}.html", pages.size() + 1, stem), gameTitle(*g), gameSubtitle(*g)};
        if (!write(folder / page.file, gamesHtml({*g}, options, look, page.title)))
            return trf("Cannot write {}", text(folder / page.file));
        pages.push_back(std::move(page));
    }
    return write(folder / "index.html", gamesIndexHtml(pages, look, title))
               ? std::string()
               : trf("Cannot write {}", text(folder / "index.html"));
}

uint64_t estimateHtml(const std::vector<HtmlGame>& sample, std::size_t games, const HtmlOptions& options,
                      const HtmlLook& look) {
    if (sample.empty()) return gamesHtml({}, options, look, "").size();
    const auto one = gamesHtml({sample.front()}, options, look, "").size();
    const auto none =
        one - gameData(sample.front(), options, 0).size() - movetextHtml(sample.front(), options, 0).size();
    uint64_t each = 0;
    for (std::size_t i = 0; i < sample.size(); ++i)
        each += gameData(sample[i], options, static_cast<int>(i)).size() +
                movetextHtml(sample[i], options, static_cast<int>(i)).size() * 12 / 10 + 80;
    each /= sample.size();
    return none + each * games;
}

}  // namespace fcg::publish
