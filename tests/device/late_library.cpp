// Owned test fixture; no target application code.
extern "C" __attribute__((visibility("default"), noinline))
int template_fixture_add(int left, int right) {
    volatile int value = left + right;
    return value;
}
