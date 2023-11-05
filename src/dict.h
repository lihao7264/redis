/* Hash Tables Implementation.
 *
 * This file implements in-memory hash tables with insert/del/replace/find/
 * get-random-element operations. Hash tables will auto-resize if needed
 * tables of power of two in size are used, collisions are handled by
 * chaining. See the source code for more information... :)
 *
 * Copyright (c) 2006-2012, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
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

#ifndef __DICT_H
#define __DICT_H

#include "mt19937-64.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

#define DICT_OK 0
#define DICT_ERR 1

// 字典中的一个节点
typedef struct dictEntry {
    // 键值对中的Key，实际上指向一个sds实例
    void *key;
     /**
      * 用于存储键值对中的value值
      * 因为是一个union，所以下面四个字段同时只会有一个有值
      */
    union {
        // 当value是一个非数字类型的值时，使用该指针
        void *val;
        // 当value值是一个无符号整数时，使用u64字段进行存储
        uint64_t u64;
         // 当value值是一个有符号整数时，使用s64字段进行存储
        int64_t s64;
        // 当value值是一个浮点数时，使用d字段进行存储
        double d;
    } v;
    // 指向下一个节点的指针
    struct dictEntry *next;     /* Next entry in the same hash bucket. */
    // 额外空间：跟Redis Cluster相关
    void *metadata[];           /* An arbitrary number of bytes (starting at a
                                 * pointer-aligned address) of size as returned
                                 * by dictType's dictEntryMetadataBytes(). */
} dictEntry;

typedef struct dict dict;

// dict 的类型
typedef struct dictType {
    // hashFunction函数用于计算key的hash值
    uint64_t (*hashFunction)(const void *key);
    // keyDup和valDup分别负责对key和value进行复制
    void *(*keyDup)(dict *d, const void *key);
    void *(*valDup)(dict *d, const void *obj);
    // 用于比较两个key是否相同
    int (*keyCompare)(dict *d, const void *key1, const void *key2);
    // keyDestructor和valDestructor分别负责销毁key和value 
    void (*keyDestructor)(dict *d, void *key);
    void (*valDestructor)(dict *d, void *obj);
     // 用于检查当前dict是否需要扩容 
    int (*expandAllowed)(size_t moreMem, double usedRatio);
    /* Allow a dictEntry to carry extra caller-defined metadata.  The
     * extra memory is initialized to 0 when a dictEntry is allocated. */
    // 用于计算metadata那个柔性数组的长度，用于检查
    size_t (*dictEntryMetadataBytes)(dict *d);
} dictType;

#define DICTHT_SIZE(exp) ((exp) == -1 ? 0 : (unsigned long)1<<(exp))
#define DICTHT_SIZE_MASK(exp) ((exp) == -1 ? 0 : (DICTHT_SIZE(exp))-1)

// 哈希表：字典结构
struct dict {
    // 当前dict实例使用的一些特殊函数集合，通过这些函数可改变当前dict的行为
    dictType *type;
    /*
    真正存储数据的2个hashtable（哈希表）
    1个在rehash时使用
    1个实现渐进式的rehash
    */
    dictEntry **ht_table[2];
    // 每个哈希table中存了多少个元素
    unsigned long ht_used[2];
    // 渐进式rehash现在处理的哈希槽索引值
    long rehashidx; /* rehashing not in progress if rehashidx == -1 */

    /* Keep small vars at end for optimal (minimal) struct padding */
    // 用于暂停渐进式rehash的开关
    int16_t pauserehash; /* If >0 rehashing is paused (<0 indicates coding error) */
     // 记录两个哈希table的长度（记录2的n次方中的 n 值）
    signed char ht_size_exp[2]; /* exponent of size. (size = 1<<exp) */
};

/* If safe is set to 1 this is a safe iterator, that means, you can call
 * dictAdd, dictFind, and other functions against the dictionary even while
 * iterating. Otherwise it is a non safe iterator, and only dictNext()
 * should be called while iterating. */
// dict 迭代器
typedef struct dictIterator {
    dict *d;  // 当前迭代的dict实例
    long index; // 当前迭代到的槽位
    // table：当前迭代到的dictht，取值只能是0或1
    // safe：标识当前迭代器是否为安全模式迭代器
    int table, safe;
    // 指向当前迭代到的节点及其下一个节点 
    dictEntry *entry, *nextEntry;
    /* unsafe iterator fingerprint for misuse detection. */
    // 为非安全模式迭代器设计的一个指纹标识 
    unsigned long long fingerprint;
} dictIterator;

typedef void (dictScanFunction)(void *privdata, const dictEntry *de);
typedef void (dictScanBucketFunction)(dict *d, dictEntry **bucketref);

/* This is the initial size of every hash table */
#define DICT_HT_INITIAL_EXP      2
#define DICT_HT_INITIAL_SIZE     (1<<(DICT_HT_INITIAL_EXP))

/* ------------------------------- Macros ------------------------------------*/
#define dictFreeVal(d, entry) \
    if ((d)->type->valDestructor) \
        (d)->type->valDestructor((d), (entry)->v.val)
// 设置 dictEntry 的 v 字段
#define dictSetVal(d, entry, _val_) do { \
    // 有 valDup 函数，则存储拷贝的 value
    // 没有valDup 函数，则存储原始的 value
    if ((d)->type->valDup) \
        (entry)->v.val = (d)->type->valDup((d), _val_); \
    else \
        (entry)->v.val = (_val_); \
} while(0)

#define dictSetSignedIntegerVal(entry, _val_) \
    do { (entry)->v.s64 = _val_; } while(0)

#define dictSetUnsignedIntegerVal(entry, _val_) \
    do { (entry)->v.u64 = _val_; } while(0)

#define dictSetDoubleVal(entry, _val_) \
    do { (entry)->v.d = _val_; } while(0)

#define dictFreeKey(d, entry) \
    if ((d)->type->keyDestructor) \
        (d)->type->keyDestructor((d), (entry)->key)
// 存储下 Key：是否需要复制全新的key
#define dictSetKey(d, entry, _key_) do { \
    // 是否存在keyDup函数
    if ((d)->type->keyDup) \ 
        (entry)->key = (d)->type->keyDup((d), _key_); \
    else \
        (entry)->key = (_key_); \
} while(0)

#define dictCompareKeys(d, key1, key2) \
    (((d)->type->keyCompare) ? \
        (d)->type->keyCompare((d), key1, key2) : \
        (key1) == (key2))

#define dictMetadata(entry) (&(entry)->metadata)
#define dictMetadataSize(d) ((d)->type->dictEntryMetadataBytes \
                             ? (d)->type->dictEntryMetadataBytes(d) : 0)

#define dictHashKey(d, key) (d)->type->hashFunction(key)
#define dictGetKey(he) ((he)->key)
#define dictGetVal(he) ((he)->v.val)
#define dictGetSignedIntegerVal(he) ((he)->v.s64)
#define dictGetUnsignedIntegerVal(he) ((he)->v.u64)
#define dictGetDoubleVal(he) ((he)->v.d)
#define dictSlots(d) (DICTHT_SIZE((d)->ht_size_exp[0])+DICTHT_SIZE((d)->ht_size_exp[1]))
#define dictSize(d) ((d)->ht_used[0]+(d)->ht_used[1])
// 检查 dict 是否处于 rehash 状态
// 检查 rehashidx 字段，0:从 0 开始渐进式 rehash；-1：rehash 操作全部结束
#define dictIsRehashing(d) ((d)->rehashidx != -1)
#define dictPauseRehashing(d) (d)->pauserehash++
#define dictResumeRehashing(d) (d)->pauserehash--

/* If our unsigned long type can store a 64 bit number, use a 64 bit PRNG. */
#if ULONG_MAX >= 0xffffffffffffffff
#define randomULong() ((unsigned long) genrand64_int64())
#else
#define randomULong() random()
#endif

/* API */
dict *dictCreate(dictType *type);
int dictExpand(dict *d, unsigned long size);
int dictTryExpand(dict *d, unsigned long size);
int dictAdd(dict *d, void *key, void *val);
dictEntry *dictAddRaw(dict *d, void *key, dictEntry **existing);
dictEntry *dictAddOrFind(dict *d, void *key);
int dictReplace(dict *d, void *key, void *val);
int dictDelete(dict *d, const void *key);
dictEntry *dictUnlink(dict *d, const void *key);
void dictFreeUnlinkedEntry(dict *d, dictEntry *he);
void dictRelease(dict *d);
dictEntry * dictFind(dict *d, const void *key);
void *dictFetchValue(dict *d, const void *key);
int dictResize(dict *d);
dictIterator *dictGetIterator(dict *d);
dictIterator *dictGetSafeIterator(dict *d);
dictEntry *dictNext(dictIterator *iter);
void dictReleaseIterator(dictIterator *iter);
dictEntry *dictGetRandomKey(dict *d);
dictEntry *dictGetFairRandomKey(dict *d);
unsigned int dictGetSomeKeys(dict *d, dictEntry **des, unsigned int count);
void dictGetStats(char *buf, size_t bufsize, dict *d);
uint64_t dictGenHashFunction(const void *key, size_t len);
uint64_t dictGenCaseHashFunction(const unsigned char *buf, size_t len);
void dictEmpty(dict *d, void(callback)(dict*));
void dictEnableResize(void);
void dictDisableResize(void);
int dictRehash(dict *d, int n);
int dictRehashMilliseconds(dict *d, int ms);
void dictSetHashFunctionSeed(uint8_t *seed);
uint8_t *dictGetHashFunctionSeed(void);
unsigned long dictScan(dict *d, unsigned long v, dictScanFunction *fn, dictScanBucketFunction *bucketfn, void *privdata);
uint64_t dictGetHash(dict *d, const void *key);
dictEntry **dictFindEntryRefByPtrAndHash(dict *d, const void *oldptr, uint64_t hash);

#ifdef REDIS_TEST
int dictTest(int argc, char *argv[], int flags);
#endif

#endif /* __DICT_H */
