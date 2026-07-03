#include "npc_sdb.h"

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <verilated.h>

#include "npc_memory.h"
#include "npc_runtime.h"
#include "npc_step.h"

static const char *reg_names[16] = {
  "$0", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
  "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5"
};

static bool is_batch_mode = false;

void sdb_set_batch_mode() {
  is_batch_mode = true;
}

static uint32_t npc_pc() {
  update_mem_inputs();
  return dut.dbg_pc_o;
}

static uint32_t npc_reg(int idx) {
  switch (idx) {
    case 0: return dut.dbg_x0_o;
    case 1: return dut.dbg_x1_o;
    case 2: return dut.dbg_x2_o;
    case 3: return dut.dbg_x3_o;
    case 4: return dut.dbg_x4_o;
    case 5: return dut.dbg_x5_o;
    case 6: return dut.dbg_x6_o;
    case 7: return dut.dbg_x7_o;
    case 8: return dut.dbg_x8_o;
    case 9: return dut.dbg_x9_o;
    case 10: return dut.dbg_x10_o;
    case 11: return dut.dbg_x11_o;
    case 12: return dut.dbg_x12_o;
    case 13: return dut.dbg_x13_o;
    case 14: return dut.dbg_x14_o;
    case 15: return dut.dbg_x15_o;
    default: return 0;
  }
}

static void print_regs() {
  uint32_t pc = npc_pc();
  std::printf("pc\t0x%08x\t%u\n", pc, pc);
  for (int i = 0; i < 16; ++i) {
    uint32_t val = npc_reg(i);
    std::printf("%-3s\t0x%08x\t%u\n", reg_names[i], val, val);
  }
}

static bool reg_str2val(std::string name, uint32_t *val) {
  if (!name.empty() && name[0] == '$') name.erase(0, 1);

  if (name == "pc") {
    *val = npc_pc();
    return true;
  }
  if (name == "x0" || name == "$0" || name == "0") {
    *val = npc_reg(0);
    return true;
  }
  if (name.size() > 1 && name[0] == 'x') {
    char *end = nullptr;
    errno = 0;
    long idx = std::strtol(name.c_str() + 1, &end, 10);
    if (errno == 0 && end != name.c_str() + 1 && *end == '\0' && idx >= 0 && idx < 16) {
      *val = npc_reg((int)idx);
      return true;
    }
  }
  for (int i = 0; i < 16; ++i) {
    const char *abi = reg_names[i][0] == '$' ? reg_names[i] + 1 : reg_names[i];
    if (name == abi || name == reg_names[i]) {
      *val = npc_reg(i);
      return true;
    }
  }
  return false;
}

enum class TokenKind {
  End,
  Num,
  Reg,
  LParen,
  RParen,
  Plus,
  Minus,
  Mul,
  Div,
  Eq,
  Ne,
  Lt,
  Le,
  Gt,
  Ge,
  Land,
  Lor,
  Band,
  Bor,
  Bxor,
  Lnot,
  Bnot,
};

struct Token {
  TokenKind kind;
  std::string text;
  uint32_t value = 0;
};

static bool tokenize(const std::string &expr, std::vector<Token> *tokens, std::string *err) {
  tokens->clear();
  size_t i = 0;
  while (i < expr.size()) {
    char c = expr[i];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      ++i;
      continue;
    }

    if (std::isdigit((unsigned char)c)) {
      size_t start = i;
      int base = 10;
      if (i + 1 < expr.size() && expr[i] == '0' && (expr[i + 1] == 'x' || expr[i + 1] == 'X')) {
        i += 2;
        base = 16;
        while (i < expr.size() && std::isxdigit((unsigned char)expr[i])) ++i;
      } else {
        while (i < expr.size() && std::isdigit((unsigned char)expr[i])) ++i;
      }
      std::string text = expr.substr(start, i - start);
      char *end = nullptr;
      errno = 0;
      unsigned long parsed = std::strtoul(text.c_str(), &end, base);
      if (errno != 0 || end == text.c_str() || *end != '\0' || parsed > 0xfffffffful) {
        *err = "invalid number: " + text;
        return false;
      }
      tokens->push_back({TokenKind::Num, text, (uint32_t)parsed});
      continue;
    }

    if (std::isalpha((unsigned char)c) || c == '_' || c == '$') {
      size_t start = i;
      if (expr[i] == '$') ++i;
      while (i < expr.size() && (std::isalnum((unsigned char)expr[i]) || expr[i] == '_')) ++i;
      std::string text = expr.substr(start, i - start);
      tokens->push_back({TokenKind::Reg, text, 0});
      continue;
    }

    auto two = [&](const char *s) { return i + 1 < expr.size() && expr[i] == s[0] && expr[i + 1] == s[1]; };
    if (two("==")) { tokens->push_back({TokenKind::Eq, "=="}); i += 2; continue; }
    if (two("!=")) { tokens->push_back({TokenKind::Ne, "!="}); i += 2; continue; }
    if (two("<=")) { tokens->push_back({TokenKind::Le, "<="}); i += 2; continue; }
    if (two(">=")) { tokens->push_back({TokenKind::Ge, ">="}); i += 2; continue; }
    if (two("&&")) { tokens->push_back({TokenKind::Land, "&&"}); i += 2; continue; }
    if (two("||")) { tokens->push_back({TokenKind::Lor, "||"}); i += 2; continue; }

    switch (c) {
      case '(': tokens->push_back({TokenKind::LParen, "("}); break;
      case ')': tokens->push_back({TokenKind::RParen, ")"}); break;
      case '+': tokens->push_back({TokenKind::Plus, "+"}); break;
      case '-': tokens->push_back({TokenKind::Minus, "-"}); break;
      case '*': tokens->push_back({TokenKind::Mul, "*"}); break;
      case '/': tokens->push_back({TokenKind::Div, "/"}); break;
      case '<': tokens->push_back({TokenKind::Lt, "<"}); break;
      case '>': tokens->push_back({TokenKind::Gt, ">"}); break;
      case '&': tokens->push_back({TokenKind::Band, "&"}); break;
      case '|': tokens->push_back({TokenKind::Bor, "|"}); break;
      case '^': tokens->push_back({TokenKind::Bxor, "^"}); break;
      case '!': tokens->push_back({TokenKind::Lnot, "!"}); break;
      case '~': tokens->push_back({TokenKind::Bnot, "~"}); break;
      default:
        *err = std::string("unexpected character: ") + c;
        return false;
    }
    ++i;
  }
  tokens->push_back({TokenKind::End, ""});
  return true;
}

class ExprParser {
 public:
  explicit ExprParser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

  bool eval(uint32_t *value, std::string *err) {
    err_ = err;
    uint32_t val = parse_lor();
    if (failed_) return false;
    if (peek().kind != TokenKind::End) {
      fail("trailing token: " + peek().text);
      return false;
    }
    *value = val;
    return true;
  }

 private:
  const Token &peek() const { return tokens_[pos_]; }
  bool accept(TokenKind kind) {
    if (peek().kind == kind) {
      ++pos_;
      return true;
    }
    return false;
  }
  void fail(const std::string &msg) {
    if (!failed_ && err_ != nullptr) *err_ = msg;
    failed_ = true;
  }

  uint32_t parse_lor() {
    uint32_t lhs = parse_land();
    while (!failed_ && accept(TokenKind::Lor)) lhs = (lhs != 0 || parse_land() != 0) ? 1 : 0;
    return lhs;
  }
  uint32_t parse_land() {
    uint32_t lhs = parse_bor();
    while (!failed_ && accept(TokenKind::Land)) lhs = (lhs != 0 && parse_bor() != 0) ? 1 : 0;
    return lhs;
  }
  uint32_t parse_bor() {
    uint32_t lhs = parse_bxor();
    while (!failed_ && accept(TokenKind::Bor)) lhs |= parse_bxor();
    return lhs;
  }
  uint32_t parse_bxor() {
    uint32_t lhs = parse_band();
    while (!failed_ && accept(TokenKind::Bxor)) lhs ^= parse_band();
    return lhs;
  }
  uint32_t parse_band() {
    uint32_t lhs = parse_eq();
    while (!failed_ && accept(TokenKind::Band)) lhs &= parse_eq();
    return lhs;
  }
  uint32_t parse_eq() {
    uint32_t lhs = parse_rel();
    while (!failed_) {
      if (accept(TokenKind::Eq)) lhs = lhs == parse_rel();
      else if (accept(TokenKind::Ne)) lhs = lhs != parse_rel();
      else break;
    }
    return lhs;
  }
  uint32_t parse_rel() {
    uint32_t lhs = parse_add();
    while (!failed_) {
      if (accept(TokenKind::Lt)) lhs = lhs < parse_add();
      else if (accept(TokenKind::Le)) lhs = lhs <= parse_add();
      else if (accept(TokenKind::Gt)) lhs = lhs > parse_add();
      else if (accept(TokenKind::Ge)) lhs = lhs >= parse_add();
      else break;
    }
    return lhs;
  }
  uint32_t parse_add() {
    uint32_t lhs = parse_mul();
    while (!failed_) {
      if (accept(TokenKind::Plus)) lhs += parse_mul();
      else if (accept(TokenKind::Minus)) lhs -= parse_mul();
      else break;
    }
    return lhs;
  }
  uint32_t parse_mul() {
    uint32_t lhs = parse_unary();
    while (!failed_) {
      if (accept(TokenKind::Mul)) lhs *= parse_unary();
      else if (accept(TokenKind::Div)) {
        uint32_t rhs = parse_unary();
        if (rhs == 0) { fail("division by zero"); return 0; }
        lhs /= rhs;
      } else break;
    }
    return lhs;
  }
  uint32_t parse_unary() {
    if (accept(TokenKind::Minus)) return (uint32_t)(-parse_unary());
    if (accept(TokenKind::Lnot)) return parse_unary() == 0;
    if (accept(TokenKind::Bnot)) return ~parse_unary();
    if (accept(TokenKind::Mul)) return pmem_read32(parse_unary());
    return parse_primary();
  }
  uint32_t parse_primary() {
    const Token &tok = peek();
    if (accept(TokenKind::Num)) return tok.value;
    if (accept(TokenKind::Reg)) {
      uint32_t val = 0;
      if (!reg_str2val(tok.text, &val)) {
        fail("unknown register: " + tok.text);
        return 0;
      }
      return val;
    }
    if (accept(TokenKind::LParen)) {
      uint32_t val = parse_lor();
      if (!accept(TokenKind::RParen)) fail("missing ')' ");
      return val;
    }
    fail("expected expression");
    return 0;
  }

  std::vector<Token> tokens_;
  size_t pos_ = 0;
  bool failed_ = false;
  std::string *err_ = nullptr;
};

static bool eval_expr(const std::string &expr, uint32_t *value, std::string *err) {
  std::vector<Token> tokens;
  if (!tokenize(expr, &tokens, err)) return false;
  return ExprParser(std::move(tokens)).eval(value, err);
}

struct Watchpoint {
  bool used = false;
  int no = 0;
  std::string expr;
  uint32_t last_val = 0;
};

static std::array<Watchpoint, 32> watchpoints;

static void init_watchpoints() {
  for (size_t i = 0; i < watchpoints.size(); ++i) {
    watchpoints[i].used = false;
    watchpoints[i].no = (int)i;
    watchpoints[i].expr.clear();
    watchpoints[i].last_val = 0;
  }
}

static void print_watchpoints() {
  bool any = false;
  for (const auto &wp : watchpoints) {
    if (!wp.used) continue;
    if (!any) std::printf("Num\tValue\t\tExpr\n");
    any = true;
    std::printf("%d\t0x%08x\t%s\n", wp.no, wp.last_val, wp.expr.c_str());
  }
  if (!any) std::printf("No watchpoints.\n");
}

static bool check_watchpoints() {
  bool triggered = false;
  for (auto &wp : watchpoints) {
    if (!wp.used) continue;
    uint32_t cur = 0;
    std::string err;
    if (!eval_expr(wp.expr, &cur, &err)) {
      std::printf("Watchpoint %d: failed to evaluate '%s': %s\n", wp.no, wp.expr.c_str(), err.c_str());
      continue;
    }
    if (cur != wp.last_val) {
      std::printf("Watchpoint %d triggered: %s\n", wp.no, wp.expr.c_str());
      std::printf("Old value = 0x%08x, New value = 0x%08x\n", wp.last_val, cur);
      wp.last_val = cur;
      triggered = true;
    }
  }
  return triggered;
}

static void report_trap() {
  if (!dut.trap) return;
  if (dut.trap_code == 0) {
    std::printf("HIT GOOD TRAP at pc = 0x%08x\n", npc_pc());
  } else {
    std::printf("HIT BAD TRAP at pc = 0x%08x, code = %u\n", npc_pc(), dut.trap_code);
  }
}

static void npc_exec(uint64_t n) {
  for (uint64_t i = 0; i < n && !Verilated::gotFinish() && !dut.trap; ++i) {
    if (single_cycle()) break;
    if (check_watchpoints()) break;
  }
  report_trap();
}

static int cmd_c(const std::string &) {
  while (!Verilated::gotFinish() && !dut.trap) {
    if (single_cycle()) break;
    if (check_watchpoints()) break;
  }
  report_trap();
  return 0;
}

static int cmd_q(const std::string &) {
  return -1;
}

static int cmd_si(const std::string &args) {
  uint64_t steps = 1;
  if (!args.empty()) {
    char *end = nullptr;
    errno = 0;
    unsigned long long parsed = std::strtoull(args.c_str(), &end, 0);
    if (errno != 0 || end == args.c_str() || *end != '\0') {
      std::printf("Usage: si [N]\n");
      return 0;
    }
    steps = parsed;
  }
  npc_exec(steps);
  return 0;
}

static int cmd_info(const std::string &args) {
  std::istringstream iss(args);
  std::string subcmd;
  iss >> subcmd;
  if (subcmd == "r") {
    print_regs();
  } else if (subcmd == "w") {
    print_watchpoints();
  } else {
    std::printf("Usage: info r|w\n");
  }
  return 0;
}

static int cmd_x(const std::string &args) {
  std::istringstream iss(args);
  std::string n_str;
  iss >> n_str;
  std::string expr;
  std::getline(iss, expr);
  size_t first = expr.find_first_not_of(" \t");
  if (first != std::string::npos) expr.erase(0, first);

  if (n_str.empty() || expr.empty()) {
    std::printf("Usage: x N EXPR\n");
    return 0;
  }

  char *end = nullptr;
  errno = 0;
  unsigned long n = std::strtoul(n_str.c_str(), &end, 0);
  if (errno != 0 || end == n_str.c_str() || *end != '\0') {
    std::printf("Invalid count: %s\n", n_str.c_str());
    return 0;
  }

  uint32_t addr = 0;
  std::string err;
  if (!eval_expr(expr, &addr, &err)) {
    std::printf("Invalid expression: %s\n", err.c_str());
    return 0;
  }

  for (unsigned long i = 0; i < n; ++i) {
    uint32_t data = pmem_read32(addr);
    std::printf("0x%08x: 0x%08x\n", addr, data);
    addr += 4;
  }
  return 0;
}

static int cmd_p(const std::string &args) {
  if (args.empty()) {
    std::printf("Usage: p EXPR\n");
    return 0;
  }
  uint32_t val = 0;
  std::string err;
  if (!eval_expr(args, &val, &err)) {
    std::printf("Invalid expression: %s\n", err.c_str());
    return 0;
  }
  std::printf("0x%08x\t%u\n", val, val);
  return 0;
}

static int cmd_w(const std::string &args) {
  if (args.empty()) {
    std::printf("Usage: w EXPR\n");
    return 0;
  }
  uint32_t val = 0;
  std::string err;
  if (!eval_expr(args, &val, &err)) {
    std::printf("Invalid expression: %s\n", err.c_str());
    return 0;
  }
  for (auto &wp : watchpoints) {
    if (wp.used) continue;
    wp.used = true;
    wp.expr = args;
    wp.last_val = val;
    std::printf("Watchpoint %d: %s\n", wp.no, wp.expr.c_str());
    std::printf("Initial value = 0x%08x\n", wp.last_val);
    return 0;
  }
  std::printf("No free watchpoint available. Max = %zu\n", watchpoints.size());
  return 0;
}

static int cmd_d(const std::string &args) {
  if (args.empty()) {
    std::printf("Usage: d N\n");
    return 0;
  }
  char *end = nullptr;
  errno = 0;
  long no = std::strtol(args.c_str(), &end, 0);
  if (errno != 0 || end == args.c_str() || *end != '\0') {
    std::printf("Invalid watchpoint number: %s\n", args.c_str());
    return 0;
  }
  for (auto &wp : watchpoints) {
    if (wp.used && wp.no == no) {
      wp.used = false;
      wp.expr.clear();
      wp.last_val = 0;
      return 0;
    }
  }
  std::printf("No such watchpoint %ld\n", no);
  return 0;
}

static int cmd_help(const std::string &args);

struct Command {
  const char *name;
  const char *description;
  int (*handler)(const std::string &args);
};

static Command cmd_table[] = {
  {"help", "Display information about supported commands", cmd_help},
  {"c", "Continue execution", cmd_c},
  {"q", "Exit NPC", cmd_q},
  {"si", "Single-step execution", cmd_si},
  {"info", "Print registers/watchpoints: info r|w", cmd_info},
  {"x", "Scan memory: x N EXPR", cmd_x},
  {"p", "Evaluate expression: p EXPR", cmd_p},
  {"w", "Set watchpoint: w EXPR", cmd_w},
  {"d", "Delete watchpoint: d N", cmd_d},
};

static int cmd_help(const std::string &args) {
  std::istringstream iss(args);
  std::string name;
  iss >> name;

  for (const auto &cmd : cmd_table) {
    if (name.empty() || name == cmd.name) {
      std::printf("%s - %s\n", cmd.name, cmd.description);
      if (!name.empty()) return 0;
    }
  }

  if (!name.empty()) {
    std::printf("Unknown command '%s'\n", name.c_str());
  }
  return 0;
}

void sdb_mainloop() {
  init_watchpoints();

  if (is_batch_mode) {
    cmd_c("");
    return;
  }

  std::string line;
  while (true) {
    std::cout << "(npc) " << std::flush;
    if (!std::getline(std::cin, line)) break;

    size_t first = line.find_first_not_of(" \t");
    if (first == std::string::npos) continue;
    line.erase(0, first);

    size_t split = line.find_first_of(" \t");
    std::string cmd = line.substr(0, split);
    std::string args;
    if (split != std::string::npos) {
      size_t arg_first = line.find_first_not_of(" \t", split);
      if (arg_first != std::string::npos) args = line.substr(arg_first);
    }

    bool found = false;
    for (const auto &entry : cmd_table) {
      if (cmd == entry.name) {
        found = true;
        if (entry.handler(args) < 0) return;
        break;
      }
    }

    if (!found) {
      std::printf("Unknown command '%s'\n", cmd.c_str());
    }
  }
}
