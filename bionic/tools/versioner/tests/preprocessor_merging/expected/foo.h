#if defined(__cplusplus)
extern "C" {
#endif


#if __ANDROID_API__ >= 10
int block_merging_1() __INTRODUCED_IN(10); // foo
int block_merging_2() __INTRODUCED_IN(10); /* bar */
int block_merging_3() __INTRODUCED_IN(10); /* baz
//*/
int block_merging_4() __INTRODUCED_IN(10);
#endif /* __ANDROID_API__ >= 10 */


#if defined(__cplusplus)
}
#endif
