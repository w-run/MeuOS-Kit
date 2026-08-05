/* runtime-matrix: global variable RMW. expect exit 42. */
int g = 41;
int main(void) { g++; return g; }
