// What RookStudio publishes (#93): a diagram's PNG compared with the board's own drawing, its SVG read back, the GIF
// decoded, the PDF parsed (its pages, its text through its fonts' maps, its fonts embedded), the HTML page read back;
// games and players of our own invention.

#include <doctest/doctest.hpp>

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <SDL3/SDL.h>

#include <imgui.h>
#include <nanosvg.h>
#include <nanosvgrast.h>

// A decoder of GIF, PNG and zlib of this file's own: the program's reads PNG and JPEG only.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_GIF
#define STBI_ONLY_PNG
#define STBI_SUPPORT_ZLIB
#define STBI_NO_STDIO
#include <stb_image.h>
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <study/pgn_reader.hpp>

#include "board_view.hpp"
#include "publish_doc.hpp"
#include "publish_draw.hpp"
#include "publish_gif.hpp"
#include "publish_html.hpp"
#include "publish_pdf.hpp"
#include "publish_text.hpp"
#include "renderer.hpp"

using namespace fcg;
using namespace fcg::publish;
namespace fs = std::filesystem;

namespace test_publish {

// A game of our own: the moves of a Najdorf, comments, a variation, marks, symbols, clocks, evaluations and the
// assistant's comment.
const char* GAME = R"([Event "Autumn Open 2026"]
[Site "Lyon"]
[Date "2026.10.20"]
[Round "9"]
[White "Moreau, Claire"]
[Black "Lindqvist, Erik"]
[Result "1/2-1/2"]
[ECO "B90"]

1. e4 {[%clk 1:30:00]} c5 {[%clk 1:30:00]} 2. Nf3 d6 3. d4 cxd4 4. Nxd4 Nf6 5. Nc3 a6 6. Be3 e5 7. Nb3 Be6
8. f3 Be7 9. Qd2 O-O 10. O-O-O Nbd7 11. g4 b5 12. g5 {[%eval 0.31]} b4 {[%cal Gc3e2][%csl Rb4] Each side pushes on
the other's wing.} 13. Ne2 {[%rs assistant] The knight goes to e2 to cover g3.} (13. Na4 Qa5) 13... Ne8 14. f4 a5
15. f5 $1 Bc4 16. Kb1 a4 17. Nbc1 1/2-1/2
)";

GameTree game() {
    const auto read = study::readPgnGame(GAME);
    REQUIRE(read.tree);
    return *read.tree;
}

std::shared_ptr<const PieceShapes> pieces() {
    static const auto shapes = PieceShapes::make(appearance::builtInPieces("cburnett"));
    return shapes;
}

struct Rgb {
    int r = 0, g = 0, b = 0;
};

Rgb pixel(const Image& image, int x, int y) {
    const uint8_t* p = &image.rgba[(static_cast<std::size_t>(y) * image.width + x) * 4];
    return {p[0], p[1], p[2]};
}

int differs(Rgb a, Rgb b) { return std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b); }

// A PDF read back: its objects, their dictionaries and streams (inflated).
struct PdfObject {
    std::string dict, stream;
};
std::map<int, PdfObject> readPdf(const std::string& pdf) {
    std::map<int, PdfObject> out;
    const std::regex start(R"((\d+) 0 obj\n)");
    for (auto it = std::sregex_iterator(pdf.begin(), pdf.end(), start); it != std::sregex_iterator(); ++it) {
        const int n            = std::stoi((*it)[1]);
        const std::size_t from = static_cast<std::size_t>(it->position() + it->length());
        const std::size_t end  = pdf.find("\nendobj", from);
        if (end == std::string::npos) continue;
        std::string body = pdf.substr(from, end - from);
        PdfObject o;
        if (const auto s = body.find("\nstream\n"); s != std::string::npos) {
            o.dict           = body.substr(0, s);
            std::string data = body.substr(s + 8, body.rfind("\nendstream") - s - 8);
            if (o.dict.find("/FlateDecode") != std::string::npos) {
                int length     = 0;
                char* inflated = stbi_zlib_decode_malloc(data.data(), static_cast<int>(data.size()), &length);
                REQUIRE(inflated != nullptr);
                o.stream.assign(inflated, static_cast<std::size_t>(length));
                STBI_FREE(inflated);
            } else {
                o.stream = data;
            }
        } else {
            o.dict = body;
        }
        out[n] = o;
    }
    return out;
}

int ref(const std::string& dict, const std::string& key) {
    const auto at = dict.find("/" + key + " ");
    if (at == std::string::npos) return -1;
    return std::atoi(dict.c_str() + at + key.size() + 2);
}

// Each page's text, read through the fonts' ToUnicode maps.
std::vector<std::string> pdfText(const std::map<int, PdfObject>& objects) {
    // The fonts of the resources, by name.
    std::map<std::string, std::map<int, std::u32string>> maps;
    for (const auto& [n, o] : objects) {
        const auto f = o.dict.find("/Font << ");
        if (f == std::string::npos || o.dict.find("/ProcSet") == std::string::npos) continue;
        const std::regex entry(R"(/(F\d+) (\d+) 0 R)");
        const auto fonts = o.dict.substr(f, o.dict.find(">>", f) - f);
        for (auto it = std::sregex_iterator(fonts.begin(), fonts.end(), entry); it != std::sregex_iterator(); ++it) {
            const auto& type0   = objects.at(std::stoi((*it)[2]));
            const auto& unicode = objects.at(ref(type0.dict, "ToUnicode")).stream;
            const std::regex pair(R"(<([0-9A-F]{4})> <([0-9A-F]+)>)");
            auto& map = maps[(*it)[1]];
            for (auto p = std::sregex_iterator(unicode.begin(), unicode.end(), pair); p != std::sregex_iterator();
                 ++p) {
                const auto hex = (*p)[2].str();
                std::u32string chars;
                for (std::size_t k = 0; k + 4 <= hex.size(); k += 4)
                    chars += static_cast<char32_t>(std::stoi(hex.substr(k, 4), nullptr, 16));
                map[std::stoi((*p)[1].str(), nullptr, 16)] = chars;
            }
        }
    }
    std::vector<std::string> pages;
    for (const auto& [n, o] : objects) {
        if (o.dict.find("/Type /Page ") == std::string::npos) continue;
        const auto& content = objects.at(ref(o.dict, "Contents")).stream;
        std::u32string text;
        const std::regex show(R"(/(F\d+) [0-9.]+ Tf .*? <([0-9A-F]*)> Tj)");
        for (auto it = std::sregex_iterator(content.begin(), content.end(), show); it != std::sregex_iterator(); ++it) {
            const auto& map = maps[(*it)[1]];
            const auto hex  = (*it)[2].str();
            for (std::size_t k = 0; k + 4 <= hex.size(); k += 4)
                if (const auto c = map.find(std::stoi(hex.substr(k, 4), nullptr, 16)); c != map.end())
                    text += c->second;
            text += U' ';
        }
        pages.push_back(utf8(text));
    }
    return pages;
}

std::string dumpTo() {
    const char* dir = std::getenv("FCG_PUBLISH_DUMP");
    return dir ? dir : "";
}

void dump(const std::string& name, const std::string& bytes) {
    if (dumpTo().empty()) return;
    std::ofstream(fs::path(dumpTo()) / name, std::ios::binary) << bytes;
}

}  // namespace test_publish

using namespace test_publish;

TEST_CASE("publish: a diagram's PNG is the board's drawing: its squares, its pieces, its marks") {
    const auto tree = game();
    // After 12…b4: the last move, a green arrow c3-e2, a red square on b4.
    auto spec = diagramAt(tree.main, false, 24, nullptr, appearance::BoardColours{});
    REQUIRE(spec.last_move);
    REQUIRE(spec.arrows.size() == 1);
    REQUIRE(spec.squares.size() == 1);
    const Image image = diagramImage(spec, *pieces(), 480);
    CHECK(image.width == 480);
    CHECK(image.height == 480);
    dump("diagram.png", pngBytes(image));

    // The same position drawn by drawBoard() on SDL's software renderer, off screen.
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    SDL_Window* window = render::open("publish test", 480, 480, SDL_WINDOW_HIDDEN, true);
    if (!window) {
        MESSAGE("no off screen window here, skipped: " << render::failure());
        SDL_Quit();
        return;
    }
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    render::initBackends();
    const auto file = fs::temp_directory_path() / "publish-board.png";
    {
        PieceAtlas atlas;
        REQUIRE(atlas.create(60));
        std::array<chess::Piece, 64> squares = spec.pieces;
        BoardDecor decor;
        decor.eval_white = -1;
        decor.last_move  = spec.last_move;
        decor.colours    = appearance::BoardColours{};
        for (const auto& s : spec.squares) decor.tints.push_back({chess::Square(s.from), markColour(s.colour, true)});
        for (const auto& a : spec.arrows)
            decor.arrows.push_back({chess::Move::make<chess::Move::NORMAL>(chess::Square(a.from), chess::Square(a.to)),
                                    markColour(a.colour, false)});
        for (int i = 0; i < 6; ++i) {
            render::newFrame();
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(480, 480));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::Begin("board", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground);
            drawBoard(atlas, squares, 480.0f, decor);
            ImGui::End();
            ImGui::PopStyleVar();
            ImGui::Render();
            render::draw(480, 480);
            render::present();
        }
        REQUIRE(render::screenshot(file.string(), 480, 480));
    }
    render::shutdown();
    ImGui::DestroyContext();
    SDL_Quit();
    SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);

    std::ifstream in(file, std::ios::binary);
    const std::string png((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    int w = 0, h = 0, n = 0;
    unsigned char* data = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(png.data()),
                                                static_cast<int>(png.size()), &w, &h, &n, 4);
    REQUIRE(data != nullptr);
    Image board;
    board.width  = w;
    board.height = h;
    board.rgba.assign(data, data + static_cast<std::size_t>(w) * h * 4);
    stbi_image_free(data);
    dump("board.png", pngBytes(board));

    // Every square's colour, at its top right corner (no coordinate there), as the board's.
    int squares_alike = 0;
    for (int sq = 0; sq < 64; ++sq) {
        const int col = sq % 8, row = 7 - sq / 8;
        const int x = col * 60 + 55, y = row * 60 + 4;
        if (differs(pixel(image, x, y), pixel(board, x, y)) <= 6) ++squares_alike;
    }
    CHECK(squares_alike == 64);
    // Each piece as the board's atlas draws it.
    for (int sq = 0; sq < 64; ++sq) {
        if (spec.pieces[static_cast<std::size_t>(sq)] == chess::Piece::NONE) continue;
        if (sq == spec.arrows[0].from || sq == spec.arrows[0].to) continue;  // under the arrow's ends
        const int col = sq % 8, row = 7 - sq / 8;
        long total = 0;
        for (int y = row * 60 + 10; y < row * 60 + 50; ++y)
            for (int x = col * 60 + 10; x < col * 60 + 50; ++x)
                total += differs(pixel(image, x, y), pixel(board, x, y));
        CHECK_MESSAGE(total / (40.0 * 40.0) < 10.0, "square " << sq);
    }
    // The arrow from c3 to e2 along its shaft, the red square on b4 and the last move's squares.
    const auto at       = [](int file, int rank) { return std::pair<int, int>{file * 60 + 30, (7 - rank) * 60 + 30}; };
    const auto [cx, cy] = at(2, 2);
    const auto [ex, ey] = at(4, 1);
    const int mx = (cx + ex) / 2, my = (cy + ey) / 2;
    CHECK(differs(pixel(image, mx, my), pixel(board, mx, my)) <= 12);
    CHECK(pixel(image, mx, my).g > pixel(image, mx, my).r + 30);  // green
    const auto [bx, by] = at(1, 3);
    CHECK(differs(pixel(image, bx - 26, by - 26), pixel(board, bx - 26, by - 26)) <= 6);
    CHECK(pixel(image, bx - 26, by - 26).r > pixel(image, bx - 26, by - 26).g + 30);  // red
}

TEST_CASE("publish: a diagram's SVG, read back by nanosvg, draws its PNG; sizes, caption, side to move, grey") {
    const auto tree = game();
    auto spec       = diagramAt(tree.main, false, 24, nullptr, appearance::BoardColours{});
    const auto svg  = diagramSvg(spec, *pieces(), true, 480);
    CHECK(svg.find("<svg") != std::string::npos);
    CHECK(svg.find("width=\"480\"") != std::string::npos);
    const auto bare     = diagramSvg(spec, *pieces(), false, 480);
    const auto open_tag = bare.substr(bare.find("<svg"), bare.find('>', bare.find("<svg")) - bare.find("<svg"));
    CHECK(open_tag.find(" width=") == std::string::npos);  // without a size: its viewBox only
    CHECK(open_tag.find("viewBox=") != std::string::npos);
    dump("diagram.svg", svg);

    std::string text  = svg;
    NSVGimage* parsed = nsvgParse(text.data(), "px", 96.0f);
    REQUIRE(parsed != nullptr);
    CHECK(parsed->width == doctest::Approx(480));
    Image raster;
    raster.width  = 480;
    raster.height = 480;
    raster.rgba.assign(480u * 480u * 4u, 0);
    NSVGrasterizer* rast = nsvgCreateRasterizer();
    nsvgRasterize(rast, parsed, 0, 0, 1.0f, raster.rgba.data(), 480, 480, 480 * 4);
    nsvgDeleteRasterizer(rast);
    nsvgDelete(parsed);
    Image white;
    white.width  = 480;
    white.height = 480;
    white.fill({255, 255, 255, 255});
    white.blend(raster);
    const Image png = diagramImage(spec, *pieces(), 480);
    long total      = 0;
    for (int y = 0; y < 480; ++y)
        for (int x = 0; x < 480; ++x) total += differs(pixel(white, x, y), pixel(png, x, y));
    CHECK(total / (480.0 * 480.0) < 3.0);

    // A caption and the side to move make the image larger; Black below when it moves; every colour a grey.
    spec.caption         = "Black to play and win";
    spec.side_dot        = true;
    spec.flipped         = spec.side == chess::Color::BLACK;
    spec.black_and_white = true;
    const Image grey     = diagramImage(spec, *pieces(), 480);
    CHECK(grey.width > 480);
    CHECK(grey.height > 480);
    int coloured = 0;
    for (std::size_t i = 0; i < grey.rgba.size(); i += 4)
        if (grey.rgba[i] != grey.rgba[i + 1] || grey.rgba[i + 1] != grey.rgba[i + 2]) ++coloured;
    CHECK(coloured == 0);
    // The caption written under the board: dark pixels there.
    int ink = 0;
    for (int y = 482; y < grey.height; ++y)
        for (int x = 0; x < 480; ++x) ink += pixel(grey, x, y).r < 100 ? 1 : 0;
    CHECK(ink > 200);
    // The PNG's text chunk.
    const auto bytes = pngBytes(grey, "Pieces: Cburnett");
    CHECK(bytes.find("tEXtComment") != std::string::npos);
    int w = 0, h = 0, n = 0;
    unsigned char* data = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(bytes.data()),
                                                static_cast<int>(bytes.size()), &w, &h, &n, 4);
    REQUIRE(data != nullptr);
    CHECK(w == grey.width);
    stbi_image_free(data);
}

TEST_CASE("publish: an animated GIF decoded: its frames, their delays, its size, its first frame") {
    const auto tree = game();
    GifGame g;
    g.game   = tree.main;
    g.banner = "Moreau – Lindqvist · ½-½";
    g.pieces = pieces();
    GifOptions o;
    o.size     = 240;
    o.delay_ms = 500;
    o.last_ms  = 1500;
    o.from     = 10;
    o.to       = 20;
    GifProgress progress;
    const auto gif = makeGif(g, o, &progress);
    CHECK(progress.done == 11);
    dump("game.gif", gif);
    int* delays = nullptr;
    int w = 0, h = 0, frames = 0, comp = 0;
    unsigned char* data = stbi_load_gif_from_memory(reinterpret_cast<const unsigned char*>(gif.data()),
                                                    static_cast<int>(gif.size()), &delays, &w, &h, &frames, &comp, 4);
    REQUIRE(data != nullptr);
    CHECK(frames == 11);
    CHECK(w == 240);
    CHECK(h > 240);  // the banners
    CHECK(delays[0] == 500);
    CHECK(delays[frames - 1] == 1500);
    // Its first frame: the position after 5…a6, as gifFrame() draws it, but for the palette.
    const Image first = gifFrame(g, o, 10);
    REQUIRE(first.width == w);
    REQUIRE(first.height == h);
    long total = 0;
    for (int i = 0; i < w * h; ++i)
        total += std::abs(first.rgba[static_cast<std::size_t>(i) * 4] - data[i * 4]) +
                 std::abs(first.rgba[static_cast<std::size_t>(i) * 4 + 1] - data[i * 4 + 1]) +
                 std::abs(first.rgba[static_cast<std::size_t>(i) * 4 + 2] - data[i * 4 + 2]);
    CHECK(total / static_cast<double>(w * h) < 8.0);
    STBI_FREE(data);
    STBI_FREE(delays);
    // What it weighs, estimated before: within a factor of two.
    const auto estimate = gifEstimate(g, o);
    CHECK(estimate > gif.size() / 2);
    CHECK(estimate < gif.size() * 2);
    // Cancelled: nothing.
    std::atomic<bool> cancel{true};
    CHECK(makeGif(g, o, nullptr, &cancel).empty());
}

TEST_CASE("publish: a PDF parsed: its pages, its text through its fonts' maps, its fonts embedded and cut down") {
    Document doc;
    doc.title  = "Moreau – Lindqvist";
    doc.pieces = pieces();
    doc.notice = piecesNotice("cburnett");
    doc.blocks.push_back(title("Moreau, Claire – Lindqvist, Erik · ½-½"));
    doc.blocks.push_back(subtitle("Autumn Open 2026, Lyon · round 9"));
    doc.blocks.push_back(paragraph("1.e4 c5 2.Nf3 d6 Each side pushes on the other's wing."));
    doc.blocks.push_back(paragraph("Иванов, Пётр played it too.", MUTED_COLOUR, FontId::Italic));
    const bool chinese = Fonts::get().cjk();
    if (chinese) doc.blocks.push_back(paragraph("王小明 下得好"));
    Block table;
    table.kind        = Block::Kind::Table;
    table.table.head  = {"Player", "Points"};
    table.table.align = {0, 1};
    table.table.rows  = {{{"Moreau, Claire"}, {"5½"}}, {{"Lindqvist, Erik"}, {"5"}}};
    doc.blocks.push_back(table);
    Block diagram;
    diagram.kind    = Block::Kind::Diagram;
    diagram.diagram = diagramAt(game().main, false, 24, nullptr, appearance::BoardColours{});
    diagram.caption = "after 12…b4";
    doc.blocks.push_back(diagram);
    for (int i = 0; i < 60; ++i)
        doc.blocks.push_back(paragraph("A long text that makes a second page, line " + std::to_string(i) + "."));

    const auto pages = layOut(doc);
    REQUIRE(pages.size() >= 2);
    const auto pdf = pdfBytes(pages, pieces().get(), {doc.title, "en", ""});
    dump("document.pdf", pdf);
    CHECK(pdf.rfind("%PDF-1.7", 0) == 0);
    CHECK(pdf.find("%%EOF") != std::string::npos);
    // Its cross-reference table points at its objects.
    const auto startxref = pdf.rfind("startxref\n");
    const auto xref      = static_cast<std::size_t>(std::stoll(pdf.substr(startxref + 10)));
    CHECK(pdf.compare(xref, 4, "xref") == 0);
    std::istringstream table_in(pdf.substr(xref));
    std::string word;
    int first = 0, count = 0;
    table_in >> word >> first >> count;
    std::getline(table_in, word);
    for (int i = 0; i < count; ++i) {
        std::string line;
        std::getline(table_in, line);
        if (i == 0) continue;
        const auto offset = static_cast<std::size_t>(std::stoll(line.substr(0, 10)));
        CHECK(pdf.compare(offset, std::to_string(i).size() + 6, std::to_string(i) + " 0 obj") == 0);
    }
    const auto objects = readPdf(pdf);
    int page_objects   = 0;
    for (const auto& [n, o] : objects) page_objects += o.dict.find("/Type /Page ") != std::string::npos ? 1 : 0;
    CHECK(page_objects == static_cast<int>(pages.size()));
    const auto text = pdfText(objects);
    REQUIRE(text.size() == pages.size());
    CHECK(text[0].find("Moreau, Claire") != std::string::npos);
    CHECK(text[0].find("½-½") != std::string::npos);
    CHECK(text[0].find("Each side pushes on the other's wing.") != std::string::npos);
    CHECK(text[0].find("Иванов, Пётр") != std::string::npos);
    CHECK(text[0].find("Lindqvist, Erik") != std::string::npos);
    CHECK(text.back().find("Cburnett") != std::string::npos);  // the pieces' notice, on the last page
    CHECK(text.back().find("Redistribution and use") != std::string::npos);
    if (chinese) CHECK(text[0].find("王小明") != std::string::npos);
    // Each font embedded, its program a font with the glyphs written and no others.
    int embedded = 0;
    for (const auto& [n, o] : objects) {
        if (o.dict.find("/Type /FontDescriptor") == std::string::npos) continue;
        const int file2 = ref(o.dict, "FontFile2"), file3 = ref(o.dict, "FontFile3");
        CHECK((file2 > 0 || file3 > 0));
        ++embedded;
        if (file2 > 0) {
            const auto& program = objects.at(file2).stream;
            stbtt_fontinfo info;
            REQUIRE(stbtt_InitFont(&info, reinterpret_cast<const unsigned char*>(program.data()), 0));
            stbtt_vertex* v = nullptr;
            if (o.dict.find("+Inter-Italic") != std::string::npos) {
                CHECK(stbtt_GetCodepointShape(&info, U'И', &v) > 0);  // written
                stbtt_FreeShape(&info, v);
                CHECK(stbtt_GetCodepointShape(&info, U'Q', &v) == 0);  // never written in italics
            } else if (o.dict.find("+Inter-Regular") != std::string::npos) {
                CHECK(stbtt_GetCodepointShape(&info, U'M', &v) > 0);
                stbtt_FreeShape(&info, v);
            }
        }
        if (file3 > 0) {
            CHECK(objects.at(file3).dict.find("/Subtype /CIDFontType0C") != std::string::npos);
            CHECK(objects.at(file3).stream.size() < 2000000);  // cut down from some 16 MB
        }
    }
    CHECK(embedded >= 2);
}

TEST_CASE("publish: a replayable HTML page read back: one file, its moves, variations, comments and marks") {
    HtmlGame g;
    g.tree  = game();
    g.white = "Moreau, Claire";
    g.black = "Lindqvist, Erik";
    HtmlLook look;
    look.pieces         = pieces();
    const auto notice   = piecesNotice("cburnett");
    look.pieces_notice  = notice.line;
    look.pieces_license = notice.license;
    HtmlOptions options;
    const auto page = gamesHtml({g}, options, look, gameTitle(g));
    dump("game.html", page);
    CHECK(page.rfind("<!doctype html>", 0) == 0);
    // Its moves, the variation, the comments, the arrows and squares of 12…b4.
    for (const char* s : {"\\u003eNf3\\u003c", "\\u003eNbd7\\u003c", "\\u003eNa4\\u003c", "\\u003eQa5\\u003c",
                          "Each side pushes on the other"})
        CHECK_MESSAGE(page.find(s) != std::string::npos, s);
    CHECK(page.find("class=\\\"var\\\"") != std::string::npos);
    CHECK(page.find(",r:[[") != std::string::npos);
    CHECK(page.find(",q:[[") != std::string::npos);
    CHECK(page.find("½-½") != std::string::npos);
    CHECK(page.find("Moreau, Claire") != std::string::npos);
    // The assistant's comment, only when asked, and marked.
    CHECK(page.find("cover g3") == std::string::npos);
    options.assistant = true;
    options.clocks    = true;
    options.evals     = true;
    const auto with   = gamesHtml({g}, options, look, gameTitle(g));
    CHECK(with.find("cover g3") != std::string::npos);
    CHECK(with.find("class=\\\"asl\\\"") != std::string::npos);
    CHECK(with.find("1:30:00") != std::string::npos);
    CHECK(with.find("+0.31") != std::string::npos);
    CHECK(with.find("id=\"curve\"") != std::string::npos);
    // Nothing fetched: no address but the pieces' notice (Cburnett's has none), no outside script or style.
    for (const auto* p : {&page, &with}) {
        CHECK(p->find("http") == std::string::npos);
        CHECK(p->find("src=") == std::string::npos);
        CHECK(p->find("<link") == std::string::npos);
        CHECK(p->find("@import") == std::string::npos);
        CHECK(p->find("//") == std::string::npos);
    }
    // Chessnut's notice names its license's address, and that is all.
    look.pieces_license = piecesNotice("chessnut").license;
    const auto chessnut = gamesHtml({g}, HtmlOptions{}, look, "");
    CHECK(chessnut.find("http://www.apache.org/licenses/LICENSE-2.0") != std::string::npos);
    std::size_t addresses = 0;
    for (std::size_t at = chessnut.find("http"); at != std::string::npos; at = chessnut.find("http", at + 1))
        ++addresses;
    CHECK(addresses == 1);
    // Several games: a list to choose them; and their size estimated near what it is.
    const auto two = gamesHtml({g, g}, options, look, "Two");
    CHECK(two.find("<select id=\"games\"") != std::string::npos);
    const auto estimate = estimateHtml({g}, 2, options, look);
    CHECK(estimate > two.size() * 7 / 10);
    CHECK(estimate < two.size() * 13 / 10);
}

TEST_CASE("publish: beyond a page's games, a page per game in a folder, and their list") {
    HtmlGame g;
    g.tree = game();
    HtmlLook look;
    look.pieces = pieces();
    std::vector<HtmlGame> games(3, g);
    games[1].white    = "Weber, Jonas";
    const auto folder = fs::temp_directory_path() / "publish-pages";
    fs::remove_all(folder);
    const auto read = [&](std::size_t i) -> std::optional<HtmlGame> { return games[i]; };
    std::atomic<int> done{0};
    REQUIRE(writeGamePages(folder, games.size(), read, HtmlOptions{}, look, "Club 2026", nullptr, &done).empty());
    CHECK(done == 3);
    CHECK(fs::is_regular_file(folder / "0001-moreau-claire-lindqvist-erik.html"));
    CHECK(fs::is_regular_file(folder / "0002-weber-jonas-lindqvist-erik.html"));
    CHECK(fs::is_regular_file(folder / "0003-moreau-claire-lindqvist-erik.html"));
    std::ifstream in(folder / "index.html");
    const std::string index((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(index.find("href=\"0002-weber-jonas-lindqvist-erik.html\"") != std::string::npos);
    CHECK(index.find("3 games, a page each") != std::string::npos);
    CHECK(index.find("http") == std::string::npos);
    CHECK(MAX_GAMES_PER_PAGE == 200);
    // Cancelled: nothing more is written.
    std::atomic<bool> cancel{true};
    CHECK_FALSE(
        writeGamePages(folder / "again", games.size(), read, HtmlOptions{}, look, "Club 2026", &cancel).empty());
    CHECK_FALSE(fs::exists(folder / "again" / "index.html"));
}
