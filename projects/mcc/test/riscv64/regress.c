int
rv64_regress(int value)
{
	char byte;
	short half;
	int word;

	byte = value;
	half = value;
	word = value;
	return byte + half + word;
}
