/* c_main.c — mcc (C driver) entry point.
 *
 * mcc compiles C by default: g_lang stays 0 and mcc_main drives the
 * shared driver in C mode.  The m++ binary links mpp_main.c instead,
 * which sets g_lang=1 before calling mcc_main.  Keeping the two entry
 * points in separate objects lets mcc and m++ link distinct mains from
 * the same FE_OBJS archive. */
int mcc_main(int argc, char *argv[]);

int
main(int argc, char *argv[])
{
	return mcc_main(argc, argv);
}
