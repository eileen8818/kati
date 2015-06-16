#include "parser.h"

#include "ast.h"
#include "file.h"
#include "loc.h"
#include "log.h"
#include "string_piece.h"
#include "value.h"

enum struct ParserState {
  NOT_AFTER_RULE = 0,
  AFTER_RULE,
  MAYBE_AFTER_RULE,
};

class Parser {
 public:
  Parser(StringPiece buf, const char* filename, vector<AST*>* asts)
      : buf_(buf),
        state_(ParserState::NOT_AFTER_RULE),
        out_asts_(asts),
        loc_(filename, 0),
        fixed_lineno_(false) {
  }

  ~Parser() {
  }

  void Parse() {
    l_ = 0;

    for (l_ = 0; l_ < buf_.size();) {
      size_t lf_cnt = 0;
      size_t e = FindEndOfLine(&lf_cnt);
      if (!fixed_lineno_)
        loc_.lineno += lf_cnt;
      StringPiece line(buf_.data() + l_, e - l_);
      ParseLine(line);
      if (e == buf_.size())
        break;

      l_ = e + 1;
    }
  }

 private:
  void Error(const string& msg) {
    ERROR("%s:%d: %s", LOCF(loc_), msg.c_str());
  }

  size_t FindEndOfLine(size_t* lf_cnt) {
    size_t e = l_;
    bool prev_backslash = false;
    for (; e < buf_.size(); e++) {
      char c = buf_[e];
      if (c == '\\') {
        prev_backslash = !prev_backslash;
      } else if (c == '\n') {
        ++*lf_cnt;
        if (!prev_backslash) {
          return e;
        }
      } else if (c != '\r') {
        prev_backslash = false;
      }
    }
    return e;
  }

  void ParseLine(StringPiece line) {
    if (line.empty() || (line.size() == 1 && line[0] == '\r'))
      return;

    if (line[0] == '\t' && state_ != ParserState::NOT_AFTER_RULE) {
      CommandAST* ast = new CommandAST();
      ast->expr = ParseExpr(line.substr(1), true);
      out_asts_->push_back(ast);
      return;
    }

    // TODO: directive.

    size_t sep = line.find_first_of(STRING_PIECE("=:"));
    if (sep == string::npos) {
      ParseRuleAST(line, sep);
    } else if (line[sep] == '=') {
      ParseAssignAST(line, sep);
    } else if (line.get(sep+1) == '=') {
      ParseAssignAST(line, sep+1);
    } else if (line[sep] == ':') {
      ParseRuleAST(line, sep);
    } else {
      CHECK(false);
    }
  }

  void ParseRuleAST(StringPiece line, size_t sep) {
    const bool is_rule = line.find(':') != string::npos;
    RuleAST* ast = new RuleAST;
    ast->set_loc(loc_);

    size_t found = line.substr(sep + 1).find_first_of("=;");
    if (found != string::npos) {
      found += sep + 1;
      ast->term = line[found];
      ast->after_term = ParseExpr(line.substr(found + 1).StripLeftSpaces(),
                                  ast->term == ';');
      ast->expr = ParseExpr(line.substr(0, found).StripSpaces(), false);
    } else {
      ast->term = 0;
      ast->after_term = NULL;
      ast->expr = ParseExpr(line.StripSpaces(), false);
    }
    out_asts_->push_back(ast);
    state_ = is_rule ? ParserState::AFTER_RULE : ParserState::MAYBE_AFTER_RULE;
  }

  void ParseAssignAST(StringPiece line, size_t sep) {
    if (sep == 0)
      Error("*** empty variable name ***");
    AssignOp op = AssignOp::EQ;
    size_t lhs_end = sep;
    switch (line[sep-1]) {
      case ':':
        lhs_end--;
        op = AssignOp::COLON_EQ;
        break;
      case '+':
        lhs_end--;
        op = AssignOp::PLUS_EQ;
        break;
      case '?':
        lhs_end--;
        op = AssignOp::QUESTION_EQ;
        break;
    }

    AssignAST* ast = new AssignAST;
    ast->set_loc(loc_);
    ast->lhs = ParseExpr(line.substr(0, lhs_end).StripSpaces(), false);
    ast->rhs = ParseExpr(line.substr(sep + 1).StripLeftSpaces(), false);
    ast->op = op;
    ast->directive = AssignDirective::NONE;
    out_asts_->push_back(ast);
    state_ = ParserState::NOT_AFTER_RULE;
  }

  StringPiece buf_;
  size_t l_;
  ParserState state_;

  vector<AST*>* out_asts_;

  Loc loc_;
  bool fixed_lineno_;
};

void Parse(Makefile* mk) {
  Parser parser(StringPiece(mk->buf(), mk->len()),
                mk->filename().c_str(),
                mk->mutable_asts());
  parser.Parse();
}