/* value_param_member_call.cc — classes with no data members passed by
 * value and used through member-function calls.
 *
 * An empty class (or a class with only member functions and no data
 * members) passed by value to a function, then its members invoked,
 * must work.  Returns 0 on success.
 */
class Empty {                      /* empty class */
};

class FuncOnly {                   /* only a non-overloaded member fn */
public:
	int f(int a) { return a + 1; }
};

class OvldOnly {                   /* only overloaded member fns */
public:
	int f(int a) { return a + 1; }
	int f(int a, int b) { return a + b; }
};

int
pass(Empty e)
{
	return 1;
}

int
runF(FuncOnly obj, int v)
{
	return obj.f(v);
}

int
runO(OvldOnly obj)
{
	return obj.f(5);
}

int
main(void)
{
	Empty e;
	if (pass(e) != 1) return 1;          /* empty class by value */
	FuncOnly fc;
	if (runF(fc, 5) != 6) return 2;      /* field-less + non-overloaded */
	OvldOnly oc;
	if (runO(oc) != 6) return 3;         /* field-less + overloaded */
	return 0;
}
