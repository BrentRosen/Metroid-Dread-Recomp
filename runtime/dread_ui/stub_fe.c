/* Linux (X11): the in-game menu (ov.cpp) is built, the Play screen / setup (fe.cpp, Direct3D 11) isn't: the game starts
   directly, configured by its .ini and the in-game menu. */
int fe_should_show(int argc, char** argv, const char* exedir, const char* exename){ (void)argc; (void)argv; (void)exedir; (void)exename; return 0; }
int fe_run(void* hwnd){ (void)hwnd; return 0; }
int fe_wndproc(void* h, unsigned m, unsigned long long w, long long l, long long* r){ (void)h; (void)m; (void)w; (void)l; (void)r; return 0; }
