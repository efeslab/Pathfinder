#define LAYOUT_NAME "intro_1"
#define MAX_BUF_LEN 10

typedef enum {
	A,
	B,
	C,
	D,
	E,
	F
}BUF_ID;
struct pmdk_root {
	char buf_A[MAX_BUF_LEN];
	char buf_B[MAX_BUF_LEN];
	char buf_C[MAX_BUF_LEN];
	char buf_D[MAX_BUF_LEN];
	char buf_E[MAX_BUF_LEN];
	char buf_F[MAX_BUF_LEN];
	// int hint;
};

struct file_root {
	char buf[MAX_BUF_LEN];
};