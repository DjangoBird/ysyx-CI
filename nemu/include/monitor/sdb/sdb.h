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

#ifndef __SDB_H__
#define __SDB_H__

#include <common.h>

// 表达式求值接口
word_t expr(char *e, bool *success);

// 监视点结构体定义
typedef struct watchpoint {
  int NO;                  // 编号
  struct watchpoint *next; // 链表指针

  char expr[256];          // 被监视的表达式字符串
  word_t last_val;         // 上一次求值得到的结果
} WP;

// 监视点池相关接口
void init_wp_pool(void);
WP *new_wp(void);
void free_wp(WP *wp);

// 在每条指令后检查所有监视点, 如有变化会设置 nemu_state 并打印信息
void check_watchpoints(void);

// 打印当前所有监视点信息, 供 `info w` 使用
void print_watchpoints(void);

// 删除编号为 no 的监视点, 成功返回 true
bool delete_watchpoint(int no);

#endif
