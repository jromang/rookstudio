#include "publish_text.hpp"

#include <cstdlib>

#include <fmt/include/fmt/format.h>

#include "appearance.hpp"
#include "i18n.hpp"

namespace fcg::publish {

std::string nagSign(uint8_t nag) {
    switch (nag) {
        case 1:
            return "!";
        case 2:
            return "?";
        case 3:
            return "!!";
        case 4:
            return "??";
        case 5:
            return "!?";
        case 6:
            return "?!";
        case 7:
        case 8:
            return "□";
        case 10:
            return "=";
        case 13:
            return "∞";
        case 14:
            return "⩲";
        case 15:
            return "⩱";
        case 16:
            return "±";
        case 17:
            return "∓";
        case 18:
            return "+−";
        case 19:
            return "−+";
        case 22:
        case 23:
            return "⨀";
        case 146:
            return "N";
        default:
            return fmt::format("${}", nag);
    }
}

std::string evalText(const fastchess::Score& score, chess::Color mover) {
    const int64_t white = mover == chess::Color::WHITE ? score.value : -score.value;
    if (score.type == fastchess::ScoreType::MATE)
        return white < 0 ? fmt::format("#−{}", -white) : fmt::format("#{}", white);
    if (white == 0) return "0.00";
    return fmt::format("{}{}.{:02}", white < 0 ? "−" : "+", std::llabs(white) / 100, std::llabs(white) % 100);
}

std::string clockText(int64_t ms) {
    const int64_t s = (std::max<int64_t>(0, ms) + 500) / 1000;
    if (s >= 3600) return fmt::format("{}:{:02}:{:02}", s / 3600, s / 60 % 60, s % 60);
    return fmt::format("{}:{:02}", s / 60, s % 60);
}

namespace {

constexpr const char* BSD = R"(Copyright (c) 2006, Colin M.L. Burnett
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following
   disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following
   disclaimer in the documentation and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote
   products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.)";

constexpr const char* APACHE = R"(Copyright Alexis Luengas

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with
the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on
an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the
specific language governing permissions and limitations under the License.)";

constexpr const char* MIT = R"(Copyright (c) Maurizio Monge

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.)";

}  // namespace

PiecesNotice piecesNotice(const std::string& set_id) {
    PiecesNotice n;
    const auto* set = appearance::pieceSet(set_id);
    if (!set || set_id == appearance::FOLDER) {
        n.line = TR("Pieces: your own set.");
        return n;
    }
    n.line               = trf("Pieces: {}, by {}, under the {} license.", set->name, set->author, set->license);
    const std::string id = set->id;
    n.license            = id == "cburnett" ? BSD : id == "chessnut" ? APACHE : MIT;
    return n;
}

}  // namespace fcg::publish
