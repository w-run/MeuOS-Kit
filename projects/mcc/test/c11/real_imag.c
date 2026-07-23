/* Test __real__ / __imag__ extension */
int main(void) {
	_Complex double z;
	__real__ z = 1.0;
	__imag__ z = 2.0;
	double r = __real__ z;
	double i = __imag__ z;
	if (r != 1.0) return 1;
	if (i != 2.0) return 2;
	return 0;
}
