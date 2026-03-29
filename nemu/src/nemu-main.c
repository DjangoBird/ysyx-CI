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

#include <common.h>
#include <stdio.h>

word_t expr(char *e, bool *success);
void init_regex();

void init_monitor(int, char *[]);
void am_init_monitor();
void engine_start();
int is_exit_status_bad();

int main(int argc, char *argv[]) {

#ifndef CONFIG_TARGET_AM
  // 表达式批量测试模式: ./build/riscv32-nemu-interpreter expr-test
  if (argc == 2 && strcmp(argv[1], "expr-test") == 0) {
    init_regex();

    FILE *fp = fopen("input", "r");
    assert(fp != NULL);

    char line[65536 + 32];
    char expr_str[65536];
    unsigned std_result;
    int cnt = 0;

    while (fgets(line, sizeof(line), fp) != NULL) {
      if (sscanf(line, "%u %[\n\t\v\f\r ]%[^\n]", &std_result, expr_str, expr_str) < 2) {
        // 简化: 再用更通用的方式
        if (sscanf(line, "%u %[^\n]", &std_result, expr_str) != 2) {
          continue;
        }
      }

      bool success = true;
      word_t nemu_result = expr(expr_str, &success);

      if (!success || nemu_result != std_result) {
        printf("mismatch: std=%u nemu=%u expr=%s\n",
               std_result, nemu_result, expr_str);
        assert(0);
      }

      cnt++;
    }

    printf("expr-test passed, %d cases.\n", cnt);
    fclose(fp);
    return 0;
  }
#endif

  /* Initialize the monitor. */
#ifdef CONFIG_TARGET_AM
  am_init_monitor();
#else
  init_monitor(argc, argv);
#endif

  /* Start engine. */
  engine_start();

  return is_exit_status_bad();
}
