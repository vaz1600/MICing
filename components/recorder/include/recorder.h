#include <sys/unistd.h>

typedef enum recorder_cmd_ {
    RECORDER_START = 0,
	RECORDER_STOP
} recorder_cmd_t;

typedef enum recorder_state_ {
    RECORDER_STATE_NO_INIT = 0,
	RECORDER_STATE_IDLE,
	RECORDER_STATE_RECORD
} recorder_state_t;

void recorder_Start(void);
void recorder_Stop(void);
void recorder_Task(void *args);
