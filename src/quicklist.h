/* quicklist.h - A generic doubly linked quicklist implementation
 *
 * Copyright (c) 2014, Matt Stancliff <matt@genges.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this quicklist of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this quicklist of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdint.h> // for UINTPTR_MAX

#ifndef __QUICKLIST_H__
#define __QUICKLIST_H__

/* Node, quicklist, and Iterator are the only data structures used currently. */

/* quicklistNode is a 32 byte struct describing a listpack for a quicklist.
 * We use bit fields keep the quicklistNode at 32 bytes.
 * count: 16 bits, max 65536 (max lp bytes is 65k, so max count actually < 32k).
 * encoding: 2 bits, RAW=1, LZF=2.
 * container: 2 bits, PLAIN=1 (a single item as char array), PACKED=2 (listpack with multiple items).
 * recompress: 1 bit, bool, true if node is temporary decompressed for usage.
 * attempted_compress: 1 bit, boolean, used for verifying during testing.
 * extra: 10 bits, free for future use; pads out the remainder of 32 bits */
typedef struct quicklistNode {
    // 指向前后quicklistNode节点的指针
    struct quicklistNode *prev;
    struct quicklistNode *next;
    // 指向listpack 或 quicklistLZF
    unsigned char *entry;
    // listpack占用字节数
    size_t sz;             /* entry size in bytes */
    // listpack中的元素个数
    unsigned int count : 16;     /* count of items in listpack */
    /**
     * encoding：表示listpack是否压缩 及 使用哪个压缩算法。
     * 目前只有两种取值：
     *    2：表示被压缩（使用LZF压缩算法）。
     *    1：表示未压缩。
     */
    unsigned int encoding : 2;   /* RAW==1 or LZF==2 */
    // container有PLAIN和PACKED两个可选值
    unsigned int container : 2;  /* PLAIN==1 or PACKED==2 */
    /**
     * 当使用类似lindex 命令查看某一个已被压缩的数据时，
     * 需把数据暂时解压，此时设置recompress=1做一个标记，等有机会再把数据重新压缩。
     */
    unsigned int recompress : 1; /* was this node previous compressed? */
     //只在测试用例中使用，在实际生产环境没用
    unsigned int attempted_compress : 1; /* node can't compress; too small */
    // 预留扩展字段，暂时无用 
    unsigned int extra : 10; /* more bits to steal for future usage */
} quicklistNode;// quicklist中的节点

/* quicklistLZF is a 8+N byte struct holding 'sz' followed by 'compressed'.
 * 'sz' is byte length of 'compressed' field.
 * 'compressed' is LZF data with total (compressed) length 'sz'
 * NOTE: uncompressed length is stored in quicklistNode->sz.
 * When quicklistNode->entry is compressed, node->entry points to a quicklistLZF */
// 使用 quicklistLZF 存储 listpack 压缩后的数据
typedef struct quicklistLZF {
    // 压缩后的字节数
    size_t sz; /* LZF size in bytes*/
    // 压缩后的具体数据
    char compressed[];
} quicklistLZF;

/* Bookmarks are padded with realloc at the end of of the quicklist struct.
 * They should only be used for very big lists if thousands of nodes were the
 * excess memory usage is negligible, and there's a real need to iterate on them
 * in portions.
 * When not used, they don't add any memory overhead, but when used and then
 * deleted, some overhead remains (to avoid resonance).
 * The number of bookmarks used should be kept to minimum since it also adds
 * overhead on node deletion (searching for a bookmark to update). */
typedef struct quicklistBookmark {
    quicklistNode *node;
    char *name;
} quicklistBookmark;

#if UINTPTR_MAX == 0xffffffff
/* 32-bit */
#   define QL_FILL_BITS 14
#   define QL_COMP_BITS 14
#   define QL_BM_BITS 4
#elif UINTPTR_MAX == 0xffffffffffffffff
/* 64-bit */
#   define QL_FILL_BITS 16
#   define QL_COMP_BITS 16
#   define QL_BM_BITS 4 /* we can encode more, but we rather limit the user
                           since they cause performance degradation. */
#else
#   error unknown arch bits count
#endif

/* quicklist is a 40 byte struct (on 64-bit systems) describing a quicklist.
 * 'count' is the number of total entries.
 * 'len' is the number of quicklist nodes.
 * 'compress' is: 0 if compression disabled, otherwise it's the number
 *                of quicklistNodes to leave uncompressed at ends of quicklist.
 * 'fill' is the user-requested (or default) fill factor.
 * 'bookmarks are an optional feature that is used by realloc this struct,
 *      so that they don't consume memory when not used. */
typedef struct quicklist {
     // 链表的头指针 
    quicklistNode *head;
    // 链表的尾指针
    quicklistNode *tail;
    // 整个quicklist中全部的元素个数，这是所有listpack中存储的元素个数之和 
    unsigned long count;        /* total count of all entries in all listpacks */
    // 整个quicklist中quicklistNode节点的个数 
    unsigned long len;          /* number of quicklistNodes */
    // fill（占用16bit）：用于记录listpack大小设置，存放list-max-listpack-size参数值 
    signed int fill : QL_FILL_BITS;       /* fill factor for individual nodes */
    // compress（占用16bit）：用于存放list-compress-depth参数值， 
    unsigned int compress : QL_COMP_BITS; /* depth of end nodes not to compress;0=off */
    // bookmark_count（占用4bit）：用于存放bookmarks数组的长度 
    unsigned int bookmark_count: QL_BM_BITS;
    // 柔性数组 
    quicklistBookmark bookmarks[];
} quicklist;

// 迭代器
typedef struct quicklistIter {
    // 指向当前quicklistIter所迭代的quicklist实例 
    quicklist *quicklist;
    // 指向当前迭代到的quicklistNode节点 
    quicklistNode *current;
     // 指向当前迭代到的listpack节点 
    unsigned char *zi; /* points to the current element */
    // 当前迭代到的entry在listpack中的偏移量（即第几个元素）
    long offset; /* offset in current listpack */
     // 当前quicklistIter的迭代方向，AL_START_HEAD为正向遍历，AL_START_TAIL为反向遍历 
    int direction;
} quicklistIter;

typedef struct quicklistEntry {
    // 所属的quicklist实例 
    const quicklist *quicklist;
    // 所属的quicklistNode节点 
    quicklistNode *node;
    // 指向对应的element（listpack中的elememt节点） 
    unsigned char *zi;
    // 如果对应值为字符串，则由value指针记录 
    unsigned char *value;
    // 如果对应值为整数，则由longval进行记录 
    long long longval;
    // 如果对应值为字符串，则sz会记录该字符串的长度 
    size_t sz;
     // 对应entry在listpack中的偏移量（即第几个元素）
    int offset;
} quicklistEntry;

#define QUICKLIST_HEAD 0
#define QUICKLIST_TAIL -1

/* quicklist node encodings */
#define QUICKLIST_NODE_ENCODING_RAW 1
#define QUICKLIST_NODE_ENCODING_LZF 2

/* quicklist compression disable */
#define QUICKLIST_NOCOMPRESS 0

/* quicklist node container formats */
#define QUICKLIST_NODE_CONTAINER_PLAIN 1
#define QUICKLIST_NODE_CONTAINER_PACKED 2

#define QL_NODE_IS_PLAIN(node) ((node)->container == QUICKLIST_NODE_CONTAINER_PLAIN)

#define quicklistNodeIsCompressed(node)                                        \
    ((node)->encoding == QUICKLIST_NODE_ENCODING_LZF)

/* Prototypes */
// 创建一个空 quicklist 实例
quicklist *quicklistCreate(void);
// 创建 quicklist 实例
quicklist *quicklistNew(int fill, int compress);
void quicklistSetCompressDepth(quicklist *quicklist, int depth);
void quicklistSetFill(quicklist *quicklist, int fill);
void quicklistSetOptions(quicklist *quicklist, int fill, int depth);
void quicklistRelease(quicklist *quicklist);
int quicklistPushHead(quicklist *quicklist, void *value, const size_t sz);
int quicklistPushTail(quicklist *quicklist, void *value, const size_t sz);
void quicklistPush(quicklist *quicklist, void *value, const size_t sz,
                   int where);
void quicklistAppendListpack(quicklist *quicklist, unsigned char *zl);
void quicklistAppendPlainNode(quicklist *quicklist, unsigned char *data, size_t sz);
void quicklistInsertAfter(quicklistIter *iter, quicklistEntry *entry,
                          void *value, const size_t sz);
void quicklistInsertBefore(quicklistIter *iter, quicklistEntry *entry,
                           void *value, const size_t sz);
void quicklistDelEntry(quicklistIter *iter, quicklistEntry *entry);
void quicklistReplaceEntry(quicklistIter *iter, quicklistEntry *entry,
                           void *data, size_t sz);
int quicklistReplaceAtIndex(quicklist *quicklist, long index, void *data,
                            const size_t sz);
int quicklistDelRange(quicklist *quicklist, const long start, const long stop);
quicklistIter *quicklistGetIterator(quicklist *quicklist, int direction);
quicklistIter *quicklistGetIteratorAtIdx(quicklist *quicklist,
                                         int direction, const long long idx);
quicklistIter *quicklistGetIteratorEntryAtIdx(quicklist *quicklist, const long long index,
                                              quicklistEntry *entry);
int quicklistNext(quicklistIter *iter, quicklistEntry *entry);
void quicklistSetDirection(quicklistIter *iter, int direction);
void quicklistReleaseIterator(quicklistIter *iter);
quicklist *quicklistDup(quicklist *orig);
void quicklistRotate(quicklist *quicklist);
int quicklistPopCustom(quicklist *quicklist, int where, unsigned char **data,
                       size_t *sz, long long *sval,
                       void *(*saver)(unsigned char *data, size_t sz));
int quicklistPop(quicklist *quicklist, int where, unsigned char **data,
                 size_t *sz, long long *slong);
unsigned long quicklistCount(const quicklist *ql);
int quicklistCompare(quicklistEntry *entry, unsigned char *p2, const size_t p2_len);
size_t quicklistGetLzf(const quicklistNode *node, void **data);
void quicklistRepr(unsigned char *ql, int full);

/* bookmarks */
int quicklistBookmarkCreate(quicklist **ql_ref, const char *name, quicklistNode *node);
int quicklistBookmarkDelete(quicklist *ql, const char *name);
quicklistNode *quicklistBookmarkFind(quicklist *ql, const char *name);
void quicklistBookmarksClear(quicklist *ql);
int quicklistisSetPackedThreshold(size_t sz);

#ifdef REDIS_TEST
int quicklistTest(int argc, char *argv[], int flags);
#endif

/* Directions for iterators */
#define AL_START_HEAD 0
#define AL_START_TAIL 1

#endif /* __QUICKLIST_H__ */
