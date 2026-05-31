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

#include "sdb.h"

#define NR_WP 32


static WP wp_pool[NR_WP] = {};
static WP *head = NULL;   // 正在使用的监视点链表
static WP *free_ = NULL;  // 空闲监视点链表

void init_wp_pool(void) {
  int i;
  for (i = 0; i < NR_WP; i ++) {
    wp_pool[i].NO = i;
    wp_pool[i].next = (i == NR_WP - 1 ? NULL : &wp_pool[i + 1]);
  }

  head = NULL;
  free_ = wp_pool;
}

// 从空闲链表中申请一个监视点, 挂到使用链表头
WP *new_wp(void) {
  if (free_ == NULL) {
    printf("No free watchpoint available. Max = %d\n", NR_WP);
    assert(0);
  }

  WP *wp = free_;
  free_ = free_->next;

  // 插入到正在使用的链表头
  wp->next = head;
  head = wp;

  wp->expr[0] = '\0';
  wp->last_val = 0;

  return wp;
}

// 释放一个监视点到空闲链表
void free_wp(WP *wp) {
  if (wp == NULL) return;

  // 先从使用链表中摘除
  if (head == wp) {
    head = wp->next;
  } else {
    WP *prev = head;
    while (prev != NULL && prev->next != wp) {
      prev = prev->next;
    }
    if (prev != NULL) {
      prev->next = wp->next;
    }
  }

  // 插回空闲链表头
  wp->next = free_;
  free_ = wp;
}

// 打印所有正在使用的监视点
void print_watchpoints(void) {
  if (head == NULL) {
    printf("No watchpoints.\n");
    return;
  }

  printf("Num\tExpr\t	Value\n");
  for (WP *wp = head; wp != NULL; wp = wp->next) {
    printf("%d\t%s\t0x" FMT_WORD "\n", wp->NO, wp->expr, wp->last_val);
  }
}

// 按编号查找监视点, 找不到返回 NULL
static WP *find_wp_by_no(int no) {
  for (WP *wp = head; wp != NULL; wp = wp->next) {
    if (wp->NO == no) return wp;
  }
  return NULL;
}

// 在每条指令执行后调用, 检查所有监视点的表达式值是否发生变化
void check_watchpoints(void) {
  if (head == NULL) return;

  for (WP *wp = head; wp != NULL; wp = wp->next) {
    bool success = true;
    word_t cur = expr(wp->expr, &success);
    if (!success) {
      printf("Watchpoint %d: failed to evaluate expression '%s'\n", wp->NO, wp->expr);
      continue;
    }

    if (cur != wp->last_val) {
      printf("Watchpoint %d triggered: %s\n", wp->NO, wp->expr);
      printf("Old value = 0x" FMT_WORD ", New value = 0x" FMT_WORD "\n",
             wp->last_val, cur);
      wp->last_val = cur;

      nemu_state.state = NEMU_STOP;
    }
  }
}

// 供 sdb 的 `d N` 命令使用: 根据编号删除监视点
bool delete_watchpoint(int no) {
  WP *wp = find_wp_by_no(no);
  if (wp == NULL) {
    return false;
  }
  free_wp(wp);
  return true;
}


