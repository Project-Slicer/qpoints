/*
 * Copyright (C) 2020, Pranith Kumar <bobby.prani@gmail.com>
 * Copyright (C) 2022, Max Xing <x@maxxsoft.net>
 *
 * Generate BBV (Basic Block Vector) for Proxy Kernel with process slicing.
 */

extern "C" {
#include "qemu-plugin.h"
}

#include <glib.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include <iostream>
#include <mutex>
#include <sstream>

/* Physical memory start address of Proxy Kernel */
#ifndef MEM_START
#define MEM_START 0x80000000
#endif

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

/* Command line arguments */
static uint64_t ckpt_func_start, ckpt_func_len;

/* Plugins need to take care of their own locking */
static std::mutex lock;
static GHashTable *hotblocks;

static uint64_t unique_trans_id = 0; /* unique id assigned to TB */
static uint64_t ckpt_exec_num = 0; /* number of times ckpt func was executed */

static bool is_first_ckpt = true;

static gzFile bbv_file;

/*
 * Counting Structure
 *
 * The internals of the TCG are not exposed to plugins so we can only
 * get the starting PC for each block. We cheat this slightly by
 * xor'ing the number of instructions to the hash to help
 * differentiate.
 */
struct ExecCount {
  uint64_t id;
  uint64_t insns;
  uint64_t exec_count;
};

static void show_usage() {
  std::cerr << "Available options:" << std::endl;
  std::cerr << "  ckpt_start=<checkpoint func start>" << std::endl;
  std::cerr << "  ckpt_len=<checkpoint func len>" << std::endl;
  std::cerr << "  [bbv_file=<BBV file name>]" << std::endl;
}

static bool parse_args(int argc, char **argv, std::string &bbv_file_name) {
#define STARTS_WITH(str, prefix) \
  (strncmp(str, prefix "=", sizeof(prefix "=") - 1) == 0)
#define VALUE_OF(str, prefix) (str + sizeof(prefix "=") - 1)
#define PARSE_ULL(var, str, prefix, prompt)                              \
  do {                                                                   \
    char *p;                                                             \
    var = strtoull(VALUE_OF(str, prefix), &p, 0);                        \
    if (*p != '\0') {                                                    \
      std::cerr << "Invalid " << prompt << ": " << VALUE_OF(str, prefix) \
                << std::endl;                                            \
      return false;                                                      \
    }                                                                    \
  } while (0)

  for (int i = 0; i < argc; ++i) {
    if (STARTS_WITH(argv[i], "ckpt_start")) {
      PARSE_ULL(ckpt_func_start, argv[i], "ckpt_start",
                "checkpoint func start");
    } else if (STARTS_WITH(argv[i], "ckpt_len")) {
      PARSE_ULL(ckpt_func_len, argv[i], "ckpt_len", "checkpoint func len");
    } else if (STARTS_WITH(argv[i], "bbv_file")) {
      bbv_file_name = VALUE_OF(argv[i], "bbv_file");
      if (bbv_file_name.empty()) {
        std::cerr << "BBV file name can not be empty" << std::endl;
        return false;
      }
    } else {
      std::cerr << "Unknown option: " << argv[i] << std::endl;
      return false;
    }
  }

#undef STARTS_WITH
#undef VALUE_OF
#undef PARSE_ULL

  return ckpt_func_start && ckpt_func_len;
}

static void plugin_init(const std::string &bbv_file_name) {
  bbv_file = gzopen(bbv_file_name.c_str(), "w");
  hotblocks = g_hash_table_new(NULL, NULL);
}

/* lock required for this function */
static void dump_bbv() {
  if (auto it = g_hash_table_get_values(hotblocks)) {
    std::ostringstream bb_stat;
    bb_stat << "T";

    for (; it; it = it->next) {
      auto rec = reinterpret_cast<ExecCount *>(it->data);
      if (rec->exec_count) {
        bb_stat << " :" << rec->id << ":" << rec->exec_count * rec->insns;
        rec->exec_count = 0;
      }
    }

    bb_stat << std::endl;
    gzwrite(bbv_file, bb_stat.str().c_str(), bb_stat.str().length());
  }
}

static void plugin_exit(qemu_plugin_id_t id, void *p) {
  lock.lock();

  if (!is_first_ckpt) dump_bbv();

  auto it = g_hash_table_get_values(hotblocks);
  if (it) g_list_free(it);

  lock.unlock();
  gzclose(bbv_file);
}

static void user_exec(unsigned int cpu_index, void *udata) {
  lock.lock();
  if (ckpt_exec_num) {
    /* skip the first checkpoint */
    if (is_first_ckpt) {
      is_first_ckpt = false;
      auto zero_exec_count = [](gpointer data, gpointer udata) {
        reinterpret_cast<ExecCount *>(data)->exec_count = 0;
      };
      g_list_foreach(g_hash_table_get_values(hotblocks), zero_exec_count, NULL);
    } else {
      dump_bbv();
    }
    ckpt_exec_num = 0;
  }
  lock.unlock();
}

static ExecCount *insert_exec_count(size_t insns, uint64_t hash) {
  lock.lock();

  auto cnt = reinterpret_cast<ExecCount *>(
      g_hash_table_lookup(hotblocks, reinterpret_cast<gconstpointer>(hash)));
  if (!cnt) {
    cnt = g_new0(ExecCount, 1);
    cnt->id = ++unique_trans_id;
    cnt->insns = insns;
    g_hash_table_insert(hotblocks, reinterpret_cast<gpointer>(hash), cnt);
  }

  lock.unlock();
  return cnt;
}

static void tb_record(qemu_plugin_id_t id, struct qemu_plugin_tb *tb) {
  uint64_t pc = qemu_plugin_tb_vaddr(tb);
  size_t insns = qemu_plugin_tb_n_insns(tb);
  uint64_t hash = pc ^ insns;

  if (pc < MEM_START) {
    auto cnt = insert_exec_count(insns, hash);

    /* count the number of instructions executed */
    qemu_plugin_register_vcpu_tb_exec_inline(tb, QEMU_PLUGIN_INLINE_ADD_U64,
                                             &cnt->exec_count, 1);
    qemu_plugin_register_vcpu_tb_exec_cb(tb, user_exec, QEMU_PLUGIN_CB_NO_REGS,
                                         reinterpret_cast<void *>(hash));
  } else if (pc >= ckpt_func_start && pc < ckpt_func_start + ckpt_func_len) {
    /* count the number of checkpoint function executed */
    qemu_plugin_register_vcpu_tb_exec_inline(tb, QEMU_PLUGIN_INLINE_ADD_U64,
                                             &ckpt_exec_num, 1);
  }
}

QEMU_PLUGIN_EXPORT
int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info, int argc,
                        char **argv) {
  std::string bbv_file_name("bbv.gz");
  if (!parse_args(argc, argv, bbv_file_name)) {
    show_usage();
    return 1;
  }
  plugin_init(bbv_file_name);

  qemu_plugin_register_vcpu_tb_trans_cb(id, tb_record);
  qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
  return 0;
}
