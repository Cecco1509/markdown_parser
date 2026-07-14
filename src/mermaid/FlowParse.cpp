#include "mermaid/FlowParse.hpp"

#include "flowchart_parser.hpp" // generated: Terminal, Value, Lexeme, ParseResult, parse()
#include "mermaid/Lexer.hpp"

#include <utility>
#include <variant>
#include <vector>

namespace mermaid {
namespace {

template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

// The lexer<->parser bridge: 1:1 map from a payload-less keyword/punctuation
// token to its grammar terminal. No default -> -Wswitch flags an unhandled kind.
Terminal punct_terminal(Punct p) {
  switch (p) {
  case Punct::Newline:     return Terminal::NEWLINE;
  case Punct::Ampersand:   return Terminal::AMP;
  case Punct::Pipe:        return Terminal::PIPE;
  case Punct::Comma:       return Terminal::COMMA;
  case Punct::TripleColon: return Terminal::TRIPLECOLON;
  case Punct::KwGraph:     return Terminal::GRAPH;
  case Punct::KwSubgraph:  return Terminal::SUBGRAPH;
  case Punct::KwEnd:       return Terminal::END;
  case Punct::KwDirection: return Terminal::DIRECTION;
  case Punct::KwStyle:     return Terminal::STYLE;
  case Punct::KwClassDef:  return Terminal::CLASSDEF;
  case Punct::KwClass:     return Terminal::CLASS;
  case Punct::KwLinkStyle: return Terminal::LINKSTYLE;
  case Punct::KwClick:     return Terminal::CLICK;
  case Punct::KwDefault:   return Terminal::DEFAULT;
  case Punct::End:         return Terminal::End;
  }
  return Terminal::End; // unreachable; silences -Wreturn-type
}

// Map the lexer token stream to (Terminal, Value) lexemes for the parser.
// std::visit forces every TokenValue alternative to be handled.
std::vector<Lexeme> to_lexemes(const std::vector<Token> &toks) {
  std::vector<Lexeme> out;
  out.reserve(toks.size());
  for (const Token &t : toks) {
    out.push_back(std::visit(
        overloaded{
            [](const IdTok &x) { return Lexeme{Terminal::NODEID, Value{x.text}}; },
            [](const StrTok &x) { return Lexeme{Terminal::STR, Value{x.text}}; },
            [](const NumTok &x) { return Lexeme{Terminal::NUM, Value{x.text}}; },
            [](const StyleBodyTok &x) {
              return Lexeme{Terminal::STYLEBODY, Value{x.text}};
            },
            [](const ShapeTok &x) { return Lexeme{Terminal::SHAPE, Value{x}}; },
            [](const LinkTok &x) { return Lexeme{Terminal::LINK, Value{x}}; },
            [](const DirTok &x) { return Lexeme{Terminal::DIR, Value{x.dir}}; },
            [](const PunctTok &x) {
              return Lexeme{punct_terminal(x.punct), Value{}};
            },
        },
        t.value));
  }
  return out;
}

} // namespace

FlowParseResult parse_flowchart(std::string_view src) {
  LexResult lr = lex(src);
  ParseResult pr = parse(to_lexemes(lr.tokens));

  FlowParseResult out;
  out.errors = std::move(lr.errors);
  for (auto &e : pr.errors) out.errors.push_back(std::move(e));
  out.ok = pr.ok && out.errors.empty();
  if (pr.ok) out.document = std::move(pr.value);
  return out;
}

} // namespace mermaid
