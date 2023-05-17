/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                            keyboard.c
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                                                    Forrest Yu, 2005
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

#include "type.h"
#include "const.h"
#include "protect.h"
#include "string.h"
#include "proc.h"
#include "tty.h"
#include "console.h"
#include "global.h"
#include "proto.h"
#include "keyboard.h"
#include "keymap.h"

PRIVATE KB_INPUT	kb_in;

PRIVATE	int	code_with_E0;
PRIVATE	int	shift_l;	/* l shift state */
PRIVATE	int	shift_r;	/* r shift state */
PRIVATE	int	alt_l;		/* l alt state	 */
PRIVATE	int	alt_r;		/* r left state	 */
PRIVATE	int	ctrl_l;		/* l ctrl state	 */
PRIVATE	int	ctrl_r;		/* l ctrl state	 */
PRIVATE	int	caps_lock;	/* Caps Lock	 */
PRIVATE	int	num_lock;	/* Num Lock	 */
PRIVATE	int	scroll_lock;	/* Scroll Lock	 */
PRIVATE	int	column;

PRIVATE int	caps_lock;	/* Caps Lock	 */
PRIVATE int	num_lock;	/* Num Lock	 */
PRIVATE int	scroll_lock;	/* Scroll Lock	 */

PRIVATE u8	get_byte_from_kbuf();
PRIVATE void    set_leds();
PRIVATE void    kb_wait();
PRIVATE void    kb_ack();

/*======================================================================*
                            keyboard_handler
 *======================================================================*/
PUBLIC void keyboard_handler(int irq)
{
	u8 scan_code = in_byte(KB_DATA);

	if (kb_in.count < KB_IN_BYTES) {
		*(kb_in.p_head) = scan_code;
		kb_in.p_head++;
		if (kb_in.p_head == kb_in.buf + KB_IN_BYTES) {
			kb_in.p_head = kb_in.buf;
		}
		kb_in.count++;
	}
}


/*======================================================================*
                           init_keyboard
*======================================================================*/
PUBLIC void init_keyboard()
{
	kb_in.count = 0;
	kb_in.p_head = kb_in.p_tail = kb_in.buf;

	shift_l	= shift_r = 0;
	alt_l	= alt_r   = 0;
	ctrl_l	= ctrl_r  = 0;

	caps_lock   = 0;
	num_lock    = 1;
	scroll_lock = 0;

	set_leds();

        put_irq_handler(KEYBOARD_IRQ, keyboard_handler);/*设定键盘中断处理程序*/
        enable_irq(KEYBOARD_IRQ);                       /*开键盘中断*/
}


/*======================================================================*
                           keyboard_read
*======================================================================*/
PUBLIC void keyboard_read(TTY* p_tty)// 公共函数，读取键盘输入，参数为终端设备结构体的指针
{
	u8	scan_code;// 定义一个无符号8位整数变量用于存放键盘扫描码
	char	output[2];// 定义一个字符数组，用于保存输出字符
	int	make;	 // 定义一个整数变量，用于标识按键是按下（1）还是释放（0）

	u32	key = 0;// 定义一个无符号32位整数变量，用于存放正在处理的键值，例如Home键被按下/释放，则key值为'HOME'
	u32*	keyrow;// 定义一个无符号32位整数指针，用于指向keymap数组的一行

	if(kb_in.count > 0){// 判断键盘输入缓冲区的计数值是否大于0，如果大于0，说明有按键输入
		code_with_E0 = 0;// 重置code_with_E0变量，用于判断是否有0xE0前缀的扫描码

		scan_code = get_byte_from_kbuf();// 从键盘缓冲区读取一个字节到scan_code

		/* 下面开始解析扫描码 */
		if (scan_code == 0xE1) {// 如果扫描码等于0xE1，说明可能按下了PauseBreak键
			int i;
			u8 pausebrk_scode[] = {0xE1, 0x1D, 0x45,
					       0xE1, 0x9D, 0xC5};// PauseBreak键的扫描码序列
			int is_pausebreak = 1;
			for(i=1;i<6;i++){
				if (get_byte_from_kbuf() != pausebrk_scode[i]) {// 遍历比较后续的扫描码是否与PauseBreak键的扫描码匹配
					is_pausebreak = 0;// 如果不匹配，说明不是PauseBreak键，退出循环
					break;
				}
			}
			if (is_pausebreak) {// 如果is_pausebreak为1，说明按下了PauseBreak键，key值设置为PAUSEBREAK
				key = PAUSEBREAK;
			}
		}
		else if (scan_code == 0xE0) {// 如果扫描码等于0xE0，说明按下的可能是特殊键（如PrintScreen）
			scan_code = get_byte_from_kbuf();// 读取下一个字节的扫描码

			/* PrintScreen 被按下 */
			if (scan_code == 0x2A) {
				if (get_byte_from_kbuf() == 0xE0) {
					if (get_byte_from_kbuf() == 0x37) {
						key = PRINTSCREEN;// PrintScreen键被按下
						make = 1;// 标记按键状态为按下
					}
				}
			}
			/* PrintScreen 被释放 */
			if (scan_code == 0xB7) {
				if (get_byte_from_kbuf() == 0xE0) {
					if (get_byte_from_kbuf() == 0xAA) {
						key = PRINTSCREEN;// PrintScreen键被释放
						make = 0;// 标记按键状态为释放
					}
				}
			}
			/* 不是PrintScreen, 此时scan_code为0xE0紧跟的那个值. */
			if (key == 0) {
				code_with_E0 = 1;// 设置code_with_E0标志，表示有0xE0前缀的扫描码
			}
		}
		if ((key != PAUSEBREAK) && (key != PRINTSCREEN)) {// 如果按下的键不是PauseBreak键和PrintScreen键
			make = (scan_code & FLAG_BREAK ? 0 : 1); // 确定按键的状态（按下或释放）

			// 定位到keymap数组中的一行
			keyrow = &keymap[(scan_code & 0x7F) * MAP_COLS];

			column = 0;

			int caps = shift_l || shift_r;// 判断是否按下了Shift键
			if (caps_lock) {// 判断是否按下了CapsLock键
				if ((keyrow[0] >= 'a') && (keyrow[0] <= 'z')){
					caps = !caps;// 如果CapsLock被按下，且当前键是字母键，取反caps
				}
			}
			if (caps) {// 如果caps为真，表示需要切换大小写，将column设为1
				column = 1;
			}

			if (code_with_E0) {// 如果code_with_E0为真，表示有0xE0前缀的扫描码，将column设为2
				column = 2;
			}

			key = keyrow[column];// 从keymap中取出key值

            // 处理Shift、Ctrl、Alt等特殊按键的情况
			switch(key) {
			case SHIFT_L:
				shift_l = make;// 更新左Shift键的状态
				break;
			case SHIFT_R:
				shift_r = make;// 更新右Shift键的状态
				break;
			case CTRL_L:
				ctrl_l = make;// 更新左Ctrl键的状态
				break;
			case CTRL_R:
				ctrl_r = make;// 更新右Ctrl键的状态
				break;
			case ALT_L:
				alt_l = make;// 更新左Alt键的状态
				break;
			case ALT_R:
				alt_l = make;// 更新右Alt键的状态
				break;
			case CAPS_LOCK:
				if (make) {
					caps_lock   = !caps_lock;// 更新CapsLock键的状态
					set_leds();// 更新LED灯状态
				}
				break;
			case NUM_LOCK:
				if (make) {
					num_lock    = !num_lock;// 更新NumLock键的状态
					set_leds();// 更新LED灯状态
				}
				break;
			case SCROLL_LOCK:
				if (make) {
					scroll_lock = !scroll_lock;// 更新ScrollLock键的状态
					set_leds();// 更新LED灯状态
				}
				break;
			default:
				break;
			}

			if (make) { // 如果是按下状态，则处理按键事件
				int pad = 0;

				/* 首先处理小键盘 */
				if ((key >= PAD_SLASH) && (key <= PAD_9)) {
					pad = 1;// 标记pad为1，表示当前键在小键盘上
					switch(key) {
					case PAD_SLASH:
						key = '/';// 小键盘上的除号对应的是'/'
						break;
					case PAD_STAR:
						key = '*';// 小键盘上的乘号对应的是'*'
						break;
					case PAD_MINUS:
						key = '-';// 小键盘上的减号对应的是'-'
						break;
					case PAD_PLUS:
						key = '+';// 小键盘上的加号对应的是'+'
						break;
					case PAD_ENTER:
						key = ENTER;
						break;
					default:
						if (num_lock &&
						    (key >= PAD_0) &&
						    (key <= PAD_9)) {
							key = key - PAD_0 + '0';// 小键盘上的数字键对应的是数字字符
						}
						else if (num_lock &&
							 (key == PAD_DOT)) {
							key = '.';// 小键盘上的小数点键对应的是'.'
						}
						else{
							switch(key) {
							case PAD_HOME:
								key = HOME;
								break;
							case PAD_END:
								key = END;
								break;
							case PAD_PAGEUP:
								key = PAGEUP;
								break;
							case PAD_PAGEDOWN:
								key = PAGEDOWN;
								break;
							case PAD_INS:
								key = INSERT;
								break;
							case PAD_UP:
								key = UP;
								break;
							case PAD_DOWN:
								key = DOWN;
								break;
							case PAD_LEFT:
								key = LEFT;
								break;
							case PAD_RIGHT:
								key = RIGHT;
								break;
							case PAD_DOT:
								key = DELETE;
								break;
							case TAB:
								key = TAB;
								break;
							default:
								break;
							}
						}
						break;
					}
				}

				key |= shift_l	? FLAG_SHIFT_L	: 0;// 如果左Shift键被按下，综合考虑该情况
				key |= shift_r	? FLAG_SHIFT_R	: 0;// 如果右Shift键被按下，综合考虑该情况
				key |= ctrl_l	? FLAG_CTRL_L	: 0;// 如果左Ctrl键被按下，综合考虑该情况
				key |= ctrl_r	? FLAG_CTRL_R	: 0;// 如果右Ctrl键被按下，综合考虑该情况
				key |= alt_l	? FLAG_ALT_L	: 0;// 如果左Alt键被按下，综合考虑该情况
				key |= alt_r	? FLAG_ALT_R	: 0;// 如果右Alt键被按下，综合考虑该情况
				key |= pad      ? FLAG_PAD      : 0;// 如果小键盘的键被按下，综合考虑该情况

				in_process(p_tty, key);// 调用in_process函数，对按键进行处理
			}
		}
	}
}

/*======================================================================*
			    get_byte_from_kbuf
 *======================================================================*/
PRIVATE u8 get_byte_from_kbuf()       /* 从键盘缓冲区中读取下一个字节 */
{
        u8 scan_code;

        while (kb_in.count <= 0) {}   /* 等待下一个字节到来 */

        disable_int();
        scan_code = *(kb_in.p_tail);
        kb_in.p_tail++;
        if (kb_in.p_tail == kb_in.buf + KB_IN_BYTES) {
                kb_in.p_tail = kb_in.buf;
        }
        kb_in.count--;
        enable_int();

	return scan_code;
}

/*======================================================================*
				 kb_wait
 *======================================================================*/
PRIVATE void kb_wait()	/* 等待 8042 的输入缓冲区空 */
{
	u8 kb_stat;

	do {
		kb_stat = in_byte(KB_CMD);
	} while (kb_stat & 0x02);
}


/*======================================================================*
				 kb_ack
 *======================================================================*/
PRIVATE void kb_ack()
{
	u8 kb_read;

	do {
		kb_read = in_byte(KB_DATA);
	} while (kb_read =! KB_ACK);
}

/*======================================================================*
				 set_leds
 *======================================================================*/
PRIVATE void set_leds()
{
	u8 leds = (caps_lock << 2) | (num_lock << 1) | scroll_lock;

	kb_wait();
	out_byte(KB_DATA, LED_CODE);
	kb_ack();

	kb_wait();
	out_byte(KB_DATA, leds);
	kb_ack();
}

