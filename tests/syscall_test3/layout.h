#define LAYOUT_NAME "intro_1"
#define MAX_BUF_LEN 10

typedef enum {
	INIT,
	A,
	B,
	C,
	D,
	E,
	F
}BUF_ID;

const char*id_2_name[] = {
	"INIT",
	"A",
	"B",
	"C",
	"D",
	"E",
	"F"
};

struct pmdk_root {
	BUF_ID id;
	// int hint;
};

struct file_root {
	char buf[MAX_BUF_LEN];
};