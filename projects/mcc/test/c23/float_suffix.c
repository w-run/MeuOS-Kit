// C23 6.4.4.2: 100f / 42F float suffix without '.' or exponent
// Must compile and run with standard C23 semantics.

int main(void) {
    // C23 decimal float suffixes without '.'
    float a = 42f;
    float b = 100F;
    float c = 0f;
    _Bool d = (sizeof(42f) == sizeof(float));
    _Bool e = (sizeof(100.0) == sizeof(double));

    if (a != 42.0f) return 1;
    if (b != 100.0f) return 2;
    if (c != 0.0f) return 3;
    if (!d) return 4;
    if (!e) return 5;

    // C23 hex float suffix
    float f = 0x1p0f;
    if (f != 1.0f) return 6;

    return 0;
}
