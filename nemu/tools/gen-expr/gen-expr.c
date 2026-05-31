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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include <string.h>

// this should be enough
static char buf[65536] = {};
static char code_buf[65536 + 128] = {}; // a little larger than `buf`
static char *code_format =
"#include <stdio.h>\n"
"int main() { "
"  unsigned result = %s; "
"  printf(\"%%u\", result); "
"  return 0; "
"}";

// 为了生成长表达式又避免 buf 溢出，使用指针和深度限制
static char *buf_p = NULL;
static char *buf_end = NULL; // 指向 buf 最后一个可写字符位置(预留'\0')

static void maybe_gen_spaces(void) {
  int n = rand() % 4; // 0~3 个空格
  while (n-- > 0 && buf_p < buf_end) {
    *buf_p++ = ' ';
  }
  *buf_p = '\0';
}

static void append_str(const char *s) {
  size_t len = strlen(s);
  if (buf_p + len > buf_end) {
    // 空间不够就尽量少写，防止溢出
    size_t can = buf_end - buf_p;
    if (can > 0) {
      memcpy(buf_p, s, can);
      buf_p += can;
      *buf_p = '\0';
    }
    return;
  }
  memcpy(buf_p, s, len);
  buf_p += len;
  *buf_p = '\0';
}

static void gen_num(void) {
  char tmp[32];
  // 保证是无符号非 0 的数，避免出现明显的 "./0" 常量
  unsigned val = (unsigned)(rand() % 1000u) + 1;  // 1~1000
  snprintf(tmp, sizeof(tmp), "%u", val);

  maybe_gen_spaces();
  append_str(tmp);
  maybe_gen_spaces();
}

static void gen_op(void) {
  const char ops[] = "+-*/"; // 包含除法，后续通过运行结果过滤除0
  char tmp[2];
  tmp[0] = ops[rand() % (sizeof(ops) - 1)];
  tmp[1] = '\0';

  maybe_gen_spaces();
  append_str(tmp);
  maybe_gen_spaces();
}

static void gen_rand_expr_rec(int depth) {
  // depth 控制表达式复杂度，避免递归太深
  if (depth <= 0) {
    gen_num();
    return;
  }

  int choice = rand() % 3;
  switch (choice) {
    case 0:
      gen_num();
      break;
    case 1:
      maybe_gen_spaces();
      if (buf_p < buf_end) {
        *buf_p++ = '(';
        *buf_p = '\0';
      }
      gen_rand_expr_rec(depth - 1);
      maybe_gen_spaces();
      if (buf_p < buf_end) {
        *buf_p++ = ')';
        *buf_p = '\0';
      }
      maybe_gen_spaces();
      break;
    default:
      gen_rand_expr_rec(depth - 1);
      gen_op();
      gen_rand_expr_rec(depth - 1);
      break;
  }
}

static void gen_rand_expr() {
  buf[0] = '\0';
  buf_p = buf;
  buf_end = buf + sizeof(buf) - 1;

  int max_depth = 8 + rand() % 5; // 8~12 层，既不太短也不至于爆栈
  gen_rand_expr_rec(max_depth);
}

int main(int argc, char *argv[]) {
  int seed = time(0);
  srand(seed);
  int loop = 1;
  if (argc > 1) {
    sscanf(argv[1], "%d", &loop);
  }
  int i;
  for (i = 0; i < loop; i ++) {
    gen_rand_expr();

    sprintf(code_buf, code_format, buf);

    FILE *fp = fopen("/tmp/.code.c", "w");
    assert(fp != NULL);
    fputs(code_buf, fp);
    fclose(fp);

    int ret = system("gcc /tmp/.code.c -o /tmp/.expr");
    if (ret != 0) continue;

    fp = popen("/tmp/.expr", "r");
    assert(fp != NULL);

    int result;
    ret = fscanf(fp, "%d", &result);
    pclose(fp);

    // 如果子程序因为除 0 等原因异常退出，没有正确打印结果，就跳过这一条
    if (ret != 1) {
      continue;
    }

    printf("%u %s\n", result, buf);
  }
  return 0;
}
