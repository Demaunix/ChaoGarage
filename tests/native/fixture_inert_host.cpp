/* An inert fixture host. Its presence in a process would prove an unexpected
   LoadLibrary occurred. No entrypoint, exports, imports, or executable logic. */
extern "C" __declspec(dllexport) const unsigned FixtureSentinel = 0x31415926;
