// For each FEN on stdin: the position as shakmaty sees it, its legal moves in shakmaty's order, each with its UCI,
// SAN (with + or #), Debug text and the FEN after it. One JSON object per line, written by hand (no serde).
use std::io::{self, BufRead, Write};

use shakmaty::fen::Fen;
use shakmaty::san::SanPlus;
use shakmaty::uci::UciMove;
use shakmaty::{CastlingMode, Chess, EnPassantMode, Position};

fn esc(s: &str) -> String {
    s.replace('\\', "\\\\").replace('"', "\\\"")
}

fn main() {
    let stdin = io::stdin();
    let mut out = io::BufWriter::new(io::stdout());
    for line in stdin.lock().lines() {
        let line = line.unwrap();
        let fen = line.trim();
        if fen.is_empty() {
            continue;
        }
        // "moves <every> <uci>...": the positions of a game from the start, one in `every`.
        if let Some(rest) = fen.strip_prefix("moves ") {
            let mut words = rest.split_whitespace();
            let every: usize = words.next().unwrap().parse().unwrap();
            let mut pos = Chess::default();
            for (ply, word) in words.enumerate() {
                if ply % every == 0 {
                    writeln!(out, "{}", record(&pos, &Fen::from_position(&pos, EnPassantMode::Always).to_string())).unwrap();
                }
                let m = word.parse::<UciMove>().unwrap().to_move(&pos).unwrap();
                pos.play_unchecked(m);
            }
            continue;
        }
        let pos: Chess = match fen.parse::<Fen>().ok().and_then(|f| f.into_position(CastlingMode::Standard).ok()) {
            Some(p) => p,
            None => {
                writeln!(out, "{{\"fen\":\"{}\",\"invalid\":true}}", esc(fen)).unwrap();
                continue;
            }
        };
        writeln!(out, "{}", record(&pos, fen)).unwrap();
    }
}

fn record(pos: &Chess, fen: &str) -> String {
    let legal_fen = Fen::from_position(pos, EnPassantMode::Legal).to_string();
    let mut moves = Vec::new();
    for m in pos.legal_moves() {
        let uci = UciMove::from_standard(m).to_string();
        let san = SanPlus::from_move(pos.clone(), m).to_string();
        let mut after = pos.clone();
        after.play_unchecked(m);
        let after_fen = Fen::from_position(&after, EnPassantMode::Legal).to_string();
        moves.push(format!(
            "{{\"uci\":\"{}\",\"san\":\"{}\",\"debug\":\"{}\",\"after\":\"{}\"}}",
            uci,
            esc(&san),
            esc(&format!("{m:?}")),
            after_fen
        ));
    }
    format!(
        "{{\"fen\":\"{}\",\"legal_fen\":\"{}\",\"check\":{},\"checkmate\":{},\"stalemate\":{},\"insufficient\":{},\"moves\":[{}]}}",
        esc(fen),
        legal_fen,
        pos.is_check(),
        pos.is_checkmate(),
        pos.is_stalemate(),
        pos.is_insufficient_material(),
        moves.join(",")
    )
}
