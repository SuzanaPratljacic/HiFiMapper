#ifndef MAPPER_SUFFIX_ARR_ALGO_HPP_
#define MAPPER_SUFFIX_ARR_ALGO_HPP_

#include <cstdlib>
#include <string>
#include <vector>
#include <ctime>

#include "biosoup/timer.hpp"

namespace mapper {

namespace detail {
// Suffix array and lcp in O(n)
// Taken from "Two Efficient Algorithms for Linear Suffix Array
// Construction.pdf" https://github.com/kurpicz/saca-bench/blob/master/sa-is/sais.cpp

#define tget(i) ((t[(i) / 8] & (1 << ((i) % 8))) ? 1 : 0)
#define tset(i, b) t[(i) / 8] = (b) ? ((1 << ((i) % 8)) | t[(i) / 8]) : ((~(1 << ((i) % 8))) & t[(i) / 8])
#define chr(i) (cs == sizeof(int) ? ((int*)s)[i] : ((unsigned char*)s)[i])
#define isLMS(i) (i > 0 && tget(i) && !tget(i - 1))

// find the start or end of each bucket
inline void getBuckets(unsigned char* s, int* bkt, int n, int K, int cs, bool end) {
  int i, sum = 0;
  for (i = 0; i <= K; i++) bkt[i] = 0;    // clear all buckets
  for (i = 0; i < n; i++) bkt[chr(i)]++;  // compute the size of each bucket
  for (i = 0; i <= K; i++) {
    sum += bkt[i];
    bkt[i] = end ? sum : sum - bkt[i];
  }
}

// compute SAl
inline void induceSAl(unsigned char* t, int* SA, unsigned char* s, int* bkt, int n, int K, int cs, bool end) {
  int i, j;
  getBuckets(s, bkt, n, K, cs, end);  // find starts of buckets
  for (i = 0; i < n; i++) {
    j = SA[i] - 1;
    if (j >= 0 && !tget(j)) SA[bkt[chr(j)]++] = j;
  }
}
// compute SAs
inline void induceSAs(unsigned char* t, int* SA, unsigned char* s, int* bkt, int n, int K, int cs, bool end) {
  int i, j;
  getBuckets(s, bkt, n, K, cs, end);  // find ends of buckets
  for (i = n - 1; i >= 0; i--) {
    j = SA[i] - 1;
    if (j >= 0 && tget(j)) SA[--bkt[chr(j)]] = j;
  }
}

// find the suffix array SA of s[0..n-1] in {1..K}^n
// require s[n-1]=0 (the sentinel!), n>=2
// use a working space (excluding s and SA) of at most 2.25n+O(1) for a constant
// alphabet
inline void SA_IS(unsigned char* s, int* SA, int n, int K, int cs) {
  int i, j;
  unsigned char* t = (unsigned char*)malloc(n / 8 + 1);  // LS-type array in bits
  // Classify the type of each character
  tset(n - 2, 0);
  tset(n - 1, 1);  // the sentinel must be in s1, important!!!
  int chunk_size = n / 8;

  //#pragma omp parallel for schedule(static, chunk_size)
  for (i = n - 3; i >= 0; i--) tset(i, (chr(i) < chr(i + 1) || (chr(i) == chr(i + 1) && tget(i + 1) == 1)) ? 1 : 0);
  // stage 1: reduce the problem by at least 1/2
  // sort all the S-substrings
  int* bkt = (int*)malloc(sizeof(int) * (K + 1));  // bucket array
  getBuckets(s, bkt, n, K, cs, true);              // find ends of buckets

  //#pragma omp parallel for num_threads(8)
  for (i = 0; i < n; i++) {
    // std::cout << std::addressof(SA) << std::endl;
    SA[i] = -1;
  }

  for (i = 1; i < n; i++)
    if (isLMS(i)) {
      SA[--bkt[chr(i)]] = i;
    }
  induceSAl(t, SA, s, bkt, n, K, cs, false);
  induceSAs(t, SA, s, bkt, n, K, cs, true);
  free(bkt);
  // compact all the sorted substrings into the first n1 items of SA
  // 2*n1 must be not larger than n (proveable)
  int n1 = 0;

  for (i = 0; i < n; i++)
    if (isLMS(SA[i])) SA[n1++] = SA[i];
  // find the lexicographic names of all substrings

  //#pragma omp parallel for num_threads(8)
  for (i = n1; i < n; i++) SA[i] = -1;  // init the name array buffer
  int name = 0, prev = -1;

  for (i = 0; i < n1; i++) {
    int pos = SA[i];
    bool diff = false;
    for (int d = 0; d < n; d++)
      if (prev == -1 || chr(pos + d) != chr(prev + d) || tget(pos + d) != tget(prev + d)) {
        diff = true;
        break;
      } else if (d > 0 && (isLMS(pos + d) || isLMS(prev + d)))
        break;
    if (diff) {
      name++;
      prev = pos;
    }
    pos = (pos % 2 == 0) ? pos / 2 : (pos - 1) / 2;
    SA[n1 + pos] = name - 1;
  }
  for (i = n - 1, j = n - 1; i >= n1; i--)
    if (SA[i] >= 0) SA[j--] = SA[i];
  // stage 2: solve the reduced problem
  // recurse if names are not yet unique
  int *SA1 = SA, *s1 = SA + n - n1;
  if (name < n1)
    SA_IS((unsigned char*)s1, SA1, n1, name - 1, sizeof(int));
  else
    // generate the suffix array of s1 directly
    //#pragma parallel for num_threads(8)
    for (i = 0; i < n1; i++) SA1[s1[i]] = i;
  // stage 3: induce the result for the original problem
  bkt = (int*)malloc(sizeof(int) * (K + 1));  // bucket array
  // put all left-most S characters into their buckets
  getBuckets(s, bkt, n, K, cs, true);  // find ends of buckets
  for (i = 1, j = 0; i < n; i++)
    if (isLMS(i))
      s1[j++] = i;  // get p1
                    //#pragma parallel for num_threads(8)
  for (i = 0; i < n1; i++)
    SA1[i] = s1[SA1[i]];                // get index in s
                                        //#pragma parallel for num_threads(8)
  for (i = n1; i < n; i++) SA[i] = -1;  // init SA[n1..n-1]
  for (i = n1 - 1; i >= 0; i--) {
    j = SA[i];
    SA[i] = -1;
    SA[--bkt[chr(j)]] = j;
  }
  induceSAl(t, SA, s, bkt, n, K, cs, false);
  induceSAs(t, SA, s, bkt, n, K, cs, true);
  free(bkt);
  free(t);
}

}  // namespace detail

template <typename T>
std::vector<uint32_t> ConstructSuffixArray(T& seq) {
  biosoup::Timer timer{};
  timer.Start();
  std::string s = seq.InflateData();
  const auto n = static_cast<std::int32_t>(s.size());
  if (n == 0) return {};
  if (n == 1) return {0};
  timer.Stop();
  timer.Start();
  std::vector<int> sa(static_cast<std::size_t>(n) + 1UL);
  detail::SA_IS(reinterpret_cast<unsigned char*>(const_cast<char*>(s.c_str())), sa.data(), n + 1, 256, 1);
  timer.Stop();
  sa.erase(sa.begin());
  return std::vector<std::uint32_t>(sa.begin(), sa.end());
}

}  // namespace mapper

#endif /* MAPPER_SUFFIX_ARR_ALGO_HPP_*/
