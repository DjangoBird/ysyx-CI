/***************************************************************************************
* Copyright (c) 2014-2024 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

#include <isa.h>

/* We use the POSIX regex functions to process regular expressions.
 * Type 'man regex' for more information about POSIX regex functions.
 */
#include <regex.h>

// ANSI 颜色代码用于终端高亮
#define ANSI_COLOR_RESET   "\033[0m"
#define ANSI_COLOR_NUM     "\033[32m"  // 数字: 绿色
#define ANSI_COLOR_REG     "\033[36m"  // 寄存器: 青色
#define ANSI_COLOR_VAR     "\033[35m"  // 变量: 紫色
#define ANSI_COLOR_OP      "\033[33m"  // 运算符: 黄色

enum {
  TK_NOTYPE = 256, //空格
  TK_EQ,
  TK_INT,
  TK_OP,
  TK_REG,
  TK_VAR,
  TK_NEG

  /* TODO: Add more token types */

};

static struct rule {
  const char *regex;
  int token_type;
} rules[] = {

  /* TODO: Add more rules.
   * Pay attention to the precedence level of different rules.
   */

  {" +", TK_NOTYPE},    // spaces
  {"\\t",TK_NOTYPE},    // tab
  {"\\+", TK_OP},         // plus
  {"==", TK_EQ},        // equal
  {"-", TK_OP},         // minus
  {"\\*",TK_OP},        // multiply
  {"/",TK_OP},          // divide
  {"\\(",'('},          // left parenthesis
  {"\\)",')'},          // right parenthesis 
  {"0[xX][0-9a-fA-F]+", TK_INT}, // hexadecimal integer
  {"[0-9]+", TK_INT},   // decimal integer
  {"\\$[a-zA-Z]+", TK_REG}, // register
  {"[a-zA-Z_][a-zA-Z0-9_]*", TK_VAR}, // variable
};

#define NR_REGEX ARRLEN(rules)

/*rules*/
static regex_t re[NR_REGEX] = {};

/* Rules are used for many times.
 * Therefore we compile them only once before any usage.
 */
void init_regex() {
  int i;
  char error_msg[128];
  int ret;

  for (i = 0; i < NR_REGEX; i ++) {
    ret = regcomp(&re[i], rules[i].regex, REG_EXTENDED);
    if (ret != 0) {
      regerror(ret, &re[i], error_msg, 128);
      panic("regex compilation failed: %s\n%s", error_msg, rules[i].regex);
    }
  }
}

typedef struct token {
  int type;
  char str[32];
} Token;

// 为了支持较长的随机表达式，将 token 缓冲区设置得足够大
static Token tokens[65536] __attribute__((used)) = {};
static int nr_token __attribute__((used))  = 0;

static bool make_token(char *e) {
  int position = 0;
  int i;
  regmatch_t pmatch;

  nr_token = 0;

  while (e[position] != '\0') {
    /* Try all rules one by one. */
    for (i = 0; i < NR_REGEX; i ++) {
      /*从头开始匹配*/
      if (regexec(&re[i], e + position, 1, &pmatch, 0) == 0 && pmatch.rm_so == 0) {
        char *substr_start = e + position;
        int substr_len = pmatch.rm_eo;

        Log("match rules[%d] = \"%s\" at position %d with len %d: %.*s",
            i, rules[i].regex, position, substr_len, substr_len, substr_start);

        position += substr_len;

        /* TODO: Now a new token is recognized with rules[i]. Add codes
         * to record the token in the array `tokens'. For certain types
         * of tokens, some extra actions should be performed.
         */

        switch (rules[i].token_type) {
          case TK_NOTYPE:
            break;
          case ')':
            tokens[nr_token].type = ')';
            strncpy(tokens[nr_token].str, substr_start, substr_len);
            tokens[nr_token].str[substr_len] = '\0';
            nr_token++;
            break;
          case '(':
            tokens[nr_token].type = '(';
            strncpy(tokens[nr_token].str, substr_start, substr_len);
            tokens[nr_token].str[substr_len] = '\0';
            nr_token++;
            break;
          case TK_EQ:
            tokens[nr_token].type = TK_EQ;
            strncpy(tokens[nr_token].str, substr_start, substr_len);
            tokens[nr_token].str[substr_len] = '\0';
            nr_token++;
            break;
          case TK_OP:
            tokens[nr_token].type = TK_OP;
            strncpy(tokens[nr_token].str, substr_start, substr_len);
            tokens[nr_token].str[substr_len] = '\0';
            nr_token++;
            break;
          case TK_VAR:
            tokens[nr_token].type = TK_VAR;
            strncpy(tokens[nr_token].str, substr_start, substr_len);
            tokens[nr_token].str[substr_len] = '\0';
            nr_token++;
            break;
          case TK_REG:
            tokens[nr_token].type = TK_REG;
            strncpy(tokens[nr_token].str, substr_start, substr_len);
            tokens[nr_token].str[substr_len] = '\0';
            nr_token++;
            break;
          case TK_INT:
            tokens[nr_token].type = TK_INT;
            if (substr_len < (int)sizeof(tokens[nr_token].str)) {
              strncpy(tokens[nr_token].str, substr_start, substr_len);
              tokens[nr_token].str[substr_len] = '\0';
            }
            else{
              printf("integer too long at position %d\n%s\n%*.s^\n", position, e, position, "");
              return false;
            }
            nr_token++;
            break;
          default: TODO();
        }

        break;
      }
    }

    if (i == NR_REGEX) {
      printf("no match at position %d\n%s\n%*.s^\n", position, e, position, "");
      return false;
    }
  }

  /* 识别一元负号：
   * 如果 '-' 出现在表达式开头，或在非数值/寄存器/变量/右括号之后，
   * 则将其从二元减号(TK_OP)改为一元负号(TK_NEG)。
   */
  for (int j = 0; j < nr_token; j++) {
    if (tokens[j].type == TK_OP && tokens[j].str[0] == '-') {
      if (j == 0) {
        tokens[j].type = TK_NEG;
      } else {
        int prev_type = tokens[j - 1].type;
        if (!(prev_type == TK_INT || prev_type == TK_REG || prev_type == TK_VAR || prev_type == ')')) {
          tokens[j].type = TK_NEG;
        }
      }
    }
  }

  return true;
}

static bool check_parentheses(int p, int q) {
  if (p >= q) {
    return false;
  }
  /*必须保证是一个完整的括号对*/
  if (tokens[p].type != '(' || tokens[q].type != ')') {
    return false;
  }

  int balance = 0;

  for (int i = p; i <= q; i++) {
    if (tokens[i].type == '(') {
      balance++;
    } else if (tokens[i].type == ')') {
      balance--;
    }

    if (balance == 0 && i < q) {
      return false;
    }

    if (balance < 0) {
      return false;
    }
  }

  return balance == 0;
}

static int find_main_op(int p, int q) {
  int op = -1;
  int min_pri = 100;
  int level = 0;

  for (int i = p; i <= q; i++) {
    int type = tokens[i].type;

    if (type == '(') {
      level++;
      continue;
    }
    if (type == ')') {
      level--;
      continue;
    }

    if (type == TK_NEG) {
      // 一元负号不作为本层主运算符参与竞争
      continue;
    }

    if (level > 0) {
      // 括号内部的运算符不参与本层主运算符的选择
      continue;
    }

    int pri = -1;

    if (type == TK_EQ) {
      pri = 1; // 最低优先级
    } else if (type == TK_OP) {
      char c = tokens[i].str[0];
      if (c == '+' || c == '-') {
        pri = 2;
      } else if (c == '*' || c == '/') {
        pri = 3;
      }
    }

    if (pri != -1 && pri <= min_pri) {
      // 同一优先级下取最右边的，保证左结合
      min_pri = pri;
      op = i;
    }
  }

  if(level != 0){
    printf("unmatched parentheses\n");
    return -1;
  }

  return op;
}

static word_t eval(int p, int q, bool *success) {
  if (p > q) {
    *success = false;
    return 0;
  }

  if (p == q) {
    Token *t = &tokens[p];

    if (t->type == TK_INT) {
      // 支持十进制和十六进制
      word_t val = 0;
      if (t->str[0] == '0' && (t->str[1] == 'x' || t->str[1] == 'X')) {
        val = (word_t)strtoul(t->str + 2, NULL, 16);
      } else {
        val = (word_t)strtoul(t->str, NULL, 10);
      }
      return val;
    }

    if (t->type == TK_REG) {
      bool ok = true;
      // 跳过前导的 '$'
      word_t val = isa_reg_str2val(t->str + 1, &ok);
      if (!ok) {
        *success = false;
        return 0;
      }
      return val;
    }

    // 暂不支持变量等其它类型
    *success = false;
    return 0;
  }

  if (check_parentheses(p, q)) {
    return eval(p + 1, q - 1, success);
  }

  int op = find_main_op(p, q);
  if (op < 0) {
    // 没有找到二元运算符，可能是以一元负号开头的表达式
    if (tokens[p].type == TK_NEG) {
      word_t val = eval(p + 1, q, success);
      if (!*success) {
        return 0;
      }
      return -val;
    }

    *success = false;
    return 0;
  }

  word_t val1 = eval(p, op - 1, success);
  if (!*success) {
    return 0;
  }
  word_t val2 = eval(op + 1, q, success);
  if (!*success) {
    return 0;
  }

  if (tokens[op].type == TK_EQ) {
    return val1 == val2;
  }

  if (tokens[op].type == TK_OP) {
    char c = tokens[op].str[0];
    switch (c) {
      case '+': return val1 + val2;
      case '-': return val1 - val2;
      case '*': return val1 * val2;
      case '/':
        if (val2 == 0) {
          Log("Division by zero in expr()\n");
          *success = false;
          return 0;
        }
        return val1 / val2;
      default:
        assert(0);
    }
  }

  *success = false;
  return 0;
}

word_t expr(char *e, bool *success) {
  if (!make_token(e)) {
    *success = false;
    return 0;
  }

  if (nr_token == 0) {
    *success = false;
    return 0;
  }

  *success = true;
  return eval(0, nr_token - 1, success);
}

// 根据 token 类型选择颜色并输出对应的字符串片段
static void print_token_with_color(int type, const char *s, int len) {
  const char *color = ANSI_COLOR_RESET;

  switch (type) {
    case TK_INT:
      color = ANSI_COLOR_NUM;
      break;
    case TK_REG:
      color = ANSI_COLOR_REG;
      break;
    case TK_VAR:
      color = ANSI_COLOR_VAR;
      break;
    case TK_EQ:
    case TK_OP:
    case TK_NEG:
      color = ANSI_COLOR_OP;
      break;
    default:
      color = ANSI_COLOR_RESET;
      break;
  }

  printf("%s%.*s" ANSI_COLOR_RESET, color, len, s);
}

// 对输入表达式做一次扫描并按 token 类型输出彩色文本
void expr_highlight(char *e) {
  int position = 0;
  int i;
  regmatch_t pmatch;

  while (e[position] != '\0') {
    for (i = 0; i < NR_REGEX; i++) {
      if (regexec(&re[i], e + position, 1, &pmatch, 0) == 0 && pmatch.rm_so == 0) {
        char *substr_start = e + position;
        int substr_len = pmatch.rm_eo;

        position += substr_len;

        int ttype = rules[i].token_type;

        if (ttype == TK_NOTYPE) {
          // 空白字符直接原样输出
          printf("%.*s", substr_len, substr_start);
        } else {
          print_token_with_color(ttype, substr_start, substr_len);
        }

        break;
      }
    }

    if (i == NR_REGEX) {
      // 无法匹配的字符，直接原样输出并前进一个字节
      putchar(e[position]);
      position++;
    }
  }

  putchar('\n');
}
