/* C23 __has_c_attribute(name): 1 when mcc recognises the standard
 * attribute, 0 otherwise (C23 6.10.1). */
#define ATTR_OK(name) __has_c_attribute(name)

extern int printf(const char *, ...);

int main(void) {
#if __has_c_attribute(nodiscard)
	printf("nodiscard: yes\n");
#else
	printf("nodiscard: no\n");
	return 1;
#endif
#if __has_c_attribute(deprecated)
	printf("deprecated: yes\n");
#else
	printf("deprecated: no\n");
	return 2;
#endif
#if __has_c_attribute(fallthrough)
	printf("fallthrough: yes\n");
#else
	printf("fallthrough: no\n");
	return 3;
#endif
#if __has_c_attribute(maybe_unused)
	printf("maybe_unused: yes\n");
#else
	printf("maybe_unused: no\n");
	return 4;
#endif
#if __has_c_attribute(noreturn)
	printf("noreturn: yes\n");
#else
	printf("noreturn: no\n");
	return 5;
#endif
	/* a vendor/unknown attribute must be reported as unsupported */
#if __has_c_attribute(not_a_real_attribute)
	return 6;
#endif
	/* usable as a constant expression in #if arithmetic */
#if !(__has_c_attribute(nodiscard) + 0 == 1)
	return 7;
#endif
	/* macro-arg form */
#if !ATTR_OK(deprecated)
	return 8;
#endif
	return 0;
}
