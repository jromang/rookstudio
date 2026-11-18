/* The oldest glibc the Linux release runs on, 2.38 (README.md), though it is built on a newer one (Ubuntu 26.04's
 * 2.43): the functions glibc gave a new version since are linked to their older one, which every glibc since keeps
 * and which a build on 2.38 links to. Included in every C and C++ source of a release build (FCG_STATIC_RUNTIME);
 * the CI fails when the program asks for a newer glibc all the same (.github/workflows/gui.yml).
 *
 * The math functions whose only older version is the first one, from `objdump -T libm.so.6` on glibc 2.43. Not
 * pinned: functions new since 2.38, which have no older version, and the termios speed functions of 2.42, whose
 * arguments changed meaning with their version. */
#if defined(__gnu_linux__) && defined(__x86_64__)
/* glibc 2.39 */
__asm__(".symver exp10,exp10@GLIBC_2.2.5");
/* glibc 2.43 */
__asm__(".symver acosf,acosf@GLIBC_2.2.5");
__asm__(".symver acoshf,acoshf@GLIBC_2.2.5");
__asm__(".symver asinf,asinf@GLIBC_2.2.5");
__asm__(".symver atan2f,atan2f@GLIBC_2.2.5");
__asm__(".symver atanhf,atanhf@GLIBC_2.2.5");
__asm__(".symver coshf,coshf@GLIBC_2.2.5");
__asm__(".symver j0f,j0f@GLIBC_2.2.5");
__asm__(".symver j1f,j1f@GLIBC_2.2.5");
__asm__(".symver jnf,jnf@GLIBC_2.2.5");
__asm__(".symver lgammaf_r,lgammaf_r@GLIBC_2.2.5");
__asm__(".symver log10f,log10f@GLIBC_2.2.5");
__asm__(".symver remainder,remainder@GLIBC_2.2.5");
__asm__(".symver remainderf,remainderf@GLIBC_2.2.5");
__asm__(".symver sinhf,sinhf@GLIBC_2.2.5");
__asm__(".symver sqrtf,sqrtf@GLIBC_2.2.5");
__asm__(".symver tgammaf,tgammaf@GLIBC_2.2.5");
__asm__(".symver y0f,y0f@GLIBC_2.2.5");
__asm__(".symver y1f,y1f@GLIBC_2.2.5");
__asm__(".symver ynf,ynf@GLIBC_2.2.5");
#endif
