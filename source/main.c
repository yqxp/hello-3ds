/* ============================================================================
 *  hello-3ds —— 一个写给「完全没接触过 3DS homebrew 的人」的入门示例
 * ============================================================================
 *
 *  这个程序做了四件事，每件都有独立的小节讲清楚：
 *
 *    ① 在两个屏幕上画字            —— citro2d
 *    ② 画一张图片                  —— tex3ds 把 PNG 转成 .t3x
 *    ③ 下屏做可以点的按钮          —— 触摸屏读取 + 命中判定
 *    ④ 后台循环播放一首 MP3        —— minimp3 解码 + NDSP 输出 + 独立线程
 *
 *  ---------------------------------------------------------------------------
 *  怎么编译
 *  ---------------------------------------------------------------------------
 *      make            → 生成 hello-3ds.3dsx
 *      make clean      → 清干净
 *
 *  需要先装 devkitPro（含 devkitARM / libctru / citro3d / citro2d）。
 *  改图片或字体时要用的 tex3ds / mkbcfnt 也一起装在 devkitPro 里了。
 *
 *  ---------------------------------------------------------------------------
 *  怎么运行
 *  ---------------------------------------------------------------------------
 *   · 模拟器：用 Azahar（Citra 的继任者）直接打开 .3dsx 文件
 *   · 真机  ：拷到 SD 卡的 /3ds/ 目录，用设备上的启动器打开
 *
 *  背景音乐需要 DSP 固件文件 sdmc:/3ds/dspfirm.cdc。
 *  该文件按许可不可随项目分发，需要使用者自行准备。
 *  没有它程序照样跑，只是没有声音。
 *
 *  ---------------------------------------------------------------------------
 *  授权：MIT
 *  © 2026 Jason (YQXP)
 * ============================================================================
 */

/* ============================================================================
 *  第 0 部分：头文件（#include 到底在干嘛）
 * ============================================================================
 *
 *  C 语言里，一个 .c 文件看不到别的文件写的东西，除非你把对方的「说明书」
 *  贴进来。那个说明书就叫头文件（.h），用 #include 贴。
 *
 *  #include <xxx.h>  → 从「系统目录」里找（这里就是 devkitPro 的库）
 *  #include "xxx.h"  → 先从「当前文件所在的目录」找，再找系统目录
 */

/* minimp3 是一个「单头文件库」：整个库就一个 .h，但同时包含了声明和实现。
 * 默认只贴声明；要真正把代码编译进来，必须先定义这个宏。
 * 所以下面这两行的顺序不能反。 */
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"        /* MP3 解码器（CC0 公共领域，可以随便用） */

#include <stdio.h>          /* printf / snprintf / fopen / fread 这些 */
#include <stdlib.h>         /* malloc / free —— 手动申请和释放内存 */
#include <string.h>         /* memset / memcpy / strlen —— 内存和字符串操作 */

#include <3ds.h>            /* libctru：3DS 的系统库。
                             * 屏幕初始化、按键读取、触摸、文件、线程、
                             * 内存分配、DSP 音频……底层全在这里。 */
#include <citro2d.h>        /* citro2d：在 GPU 上画 2D 的东西（文字、图片、色块）。
                             * 它建立在 citro3d（3D GPU 库）之上。 */

/* 一个小工具宏：算数组里有几个元素。
 * sizeof(数组) / sizeof(数组里的第一个元素) = 元素个数。
 * 这样以后往数组里加东西，不用手动改数字。 */
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))


/* ============================================================================
 *  第 1 部分：常量
 * ============================================================================
 *
 *  #define 是「编译前直接文本替换」，没有类型、没有内存、不占空间。
 *  适合用来给「魔法数字」起名字。
 */

/* --- 屏幕尺寸 ---
 * 3DS 有两个屏幕，尺寸不一样：
 *   上屏 400 × 240（裸眼 3D 的那个，不能触摸）
 *   下屏 320 × 240（触摸屏）
 * 坐标原点在左上角，x 向右、y 向下。 */
#define TOP_W    400.0f
#define BOT_W    320.0f

/* --- 颜色 ---
 * C2D_Color32(r, g, b, a) 把四个 0~255 的数打包成一个 32 位颜色值，
 * 顺序是 R、G、B、A（Alpha = 不透明度，255 表示完全不透明）。
 *
 * 颜色值用十六进制写在这里，是为了改主题时只改这一段。 */
#define CLR_BG      C2D_Color32(0x0B, 0x12, 0x24, 0xFF)  /* 上屏背景（深夜蓝） */
#define CLR_BG2     C2D_Color32(0x12, 0x1B, 0x33, 0xFF)  /* 下屏背景（稍亮一点） */
#define CLR_ACCENT  C2D_Color32(0x7C, 0xE0, 0xD8, 0xFF)  /* 强调色（冰青） */
#define CLR_TEXT    C2D_Color32(0xF2, 0xF6, 0xFF, 0xFF)  /* 正文（近白） */
#define CLR_DIM     C2D_Color32(0x8A, 0x93, 0xB0, 0xFF)  /* 次要文字（灰蓝） */
#define CLR_BTN     C2D_Color32(0x26, 0x30, 0x4E, 0xFF)  /* 按钮没被按 */
#define CLR_BTN_ON  C2D_Color32(0xFF, 0x6B, 0x9A, 0xFF)  /* 按钮被按住 */


/* ============================================================================
 *  第 2 部分：全局变量
 * ============================================================================
 *
 *  为什么这些变量写在函数外面（全局）？
 *
 *  因为在 C 里，函数之间只能通过「参数」和「返回值」传数据。
 *  但下面这些东西——字体、屏幕、音频状态——要在好几个函数里被用到，
 *  一层层传参会很啰嗦。所以干脆放在文件顶层，谁都能看见。
 *
 *  static 的作用：让这些名字只在本文件可见，别的 .c 文件看不见，
 *  避免将来程序变大时重名打架。养成习惯，全局变量一律加 static。
 */

/* --- 画面相关的三个句柄 --- */
static C3D_RenderTarget *g_top;      /* 上屏的「画布」 */
static C3D_RenderTarget *g_bot;      /* 下屏的「画布」 */
static C2D_TextBuf  g_buf;           /* 文字临时缓存（下面解释） */
static C2D_Font     g_font;          /* 中文字体；没加载成功时是 NULL */

/* --- 图片 --- */
static C2D_SpriteSheet g_logoSheet;  /* 图片包（.t3x 可以装很多张图） */
static C2D_Image       g_logo;       /* 从包里取出来的第一张图 */


/* ---------------------------------------------------------------------------
 *  2.1  音频部分要用到的全局变量
 * ---------------------------------------------------------------------------
 */

#define AUDIO_CHN   0     /* 用第 0 号声道（NDSP 一共有 24 个声道） */
#define NBUF        4     /* 准备 4 块缓冲轮着用 */

/* ndspWaveBuf 是「一块声音数据」的描述符：数据在哪、有多长、播完没有。
 * 我们准备 4 块，DSP 播第 1 块的时候 CPU 去填第 2 块，以此类推。
 * 这就是所有音频系统里的「环形缓冲 / 双缓冲」，ASIO 也是这个思路。 */
static ndspWaveBuf s_waveBufs[NBUF];

/* 这 4 块缓冲实际占用的内存。
 * 注意用的是 linearAlloc 而不是 malloc —— 原因见 audioInit() 里的注释。 */
static int16_t *s_audioBuffer = NULL;

/* 线程之间的「闹钟」。3DS 是协作式多任务：线程不会被打断，
 * 必须自己主动让出去。LightEvent 就是让线程「睡到有人叫醒」的工具。 */
static LightEvent s_event;

static volatile bool s_quit    = false;  /* 要不要退出音频线程 */
static volatile bool s_restart = false;  /* 有没有人按了「重播」 */

/* volatile 是告诉编译器：「这个变量随时可能被别人改，别自作聪明缓存它」。
 * 在多线程代码里，共享的标志位必须加 volatile，否则可能出现
 * 「主线程改了，音频线程却永远看不到」的诡异 bug。 */

/* --- 整个 MP3 文件读进内存后的位置和长度 --- */
static uint8_t *s_mp3       = NULL;
static size_t   s_mp3Size   = 0;
static size_t   s_mp3Offset = 0;    /* 已经解到文件的第几个字节 */

static mp3dec_t s_dec;              /* minimp3 的解码器状态 */

/* --- 从 MP3 头部读出来的真实参数 --- */
static int s_hz       = 44100;      /* 采样率，每秒多少个采样点 */
static int s_channels = 2;          /* 声道数：1=单声道，2=立体声 */

/* 解码时，一帧 MP3 解出来的采样数不一定刚好填满我们的缓冲。
 * 用不上的那部分先存在这里，下次接着用，这样声音才不会有断裂。 */
static int16_t s_pending[MINIMP3_MAX_SAMPLES_PER_FRAME];
static size_t  s_pendingCount = 0;  /* 这次解出来一共多少个 int16 */
static size_t  s_pendingPos   = 0;  /* 已经用掉多少个 */

/* --- 播放状态 --- */
static volatile bool  s_playing        = true;   /* 在放还是暂停 */
static volatile float s_volume         = 0.7f;   /* 音量 0.0 ~ 1.0 */
static volatile u64   s_decodedSamples = 0;      /* 已解码的采样数，用来算播放进度 */
static bool           s_audioOk        = false;  /* 音频是否初始化成功 */
static const char    *s_audioErr       = "";     /* 失败原因，用来显示在屏幕上 */
static char           s_ndspMsg[64];

/* --- 每块缓冲的容量，初始化时算好 --- */
static size_t s_bufFrames = 0;  /* 每块装多少「帧」（帧 = 每声道一个采样点） */
static size_t s_bufHalf   = 0;  /* 每块装多少个 int16（= 帧数 × 声道数） */
static bool   s_firstRun  = true;


/* ============================================================================
 *  第 3 部分：音频
 * ============================================================================
 *
 *  先把整条链路讲清楚，再看代码会顺很多：
 *
 *     romfs:/music.mp3
 *          │  fopen / fread（只做一次，开机时把整个文件读进内存）
 *          ▼
 *     内存里的 MP3 数据
 *          │  mp3dec_decode_frame()  ← 在【音频线程】里循环做
 *          ▼
 *     PCM 数据（16 位有符号整数，左右声道交错排列）
 *          │  写进 s_waveBufs[i]，然后 ndspChnWaveBufAdd()
 *          ▼
 *     NDSP 声道 0  ──►  DSP 硬件  ──►  扬声器 / 耳机
 *
 *  为什么要单独开一个线程？
 *  因为 DSP 每大约 4.9 毫秒就要一块新数据。如果解码挤在主线程里，
 *  一旦主线程忙（比如在渲染画面），声音就会断。
 *  分成两个线程后：主线程只管画，音频线程只管喂数据，互不耽误。
 */

/* ---------------------------------------------------------------------------
 *  3.1  设置音量
 * ---------------------------------------------------------------------------
 *  NDSP 的音量不是直接给一个数，而是一个 12 个 float 的「混音矩阵」：
 *  mix[0] 是左声道输出、mix[1] 是右声道输出，后面 10 个是各种效果路由，
 *  我们不用，全部填 0。这是 3DS DSP 硬件规定的格式，不是我们随便设计的。
 */
static void audioApplyVolume(void)
{
	float mix[12];
	memset(mix, 0, sizeof(mix));   /* 12 个 float 全部清零 */
	mix[0] = s_volume;             /* 左 */
	mix[1] = s_volume;             /* 右 */
	ndspChnSetMix(AUDIO_CHN, mix);
}

/* ---------------------------------------------------------------------------
 *  3.2  把一块缓冲填满 PCM 数据
 * ---------------------------------------------------------------------------
 *  被音频线程调用。参数 wb 是「哪一块缓冲要填」。
 *  返回值：true 表示填好了；false 表示彻底解不出东西了。
 */
static bool fillBuffer(ndspWaveBuf *wb)
{
	int16_t *out = wb->data_pcm16;   /* 往哪里写 */
	size_t   total = 0;              /* 已经写了多少个 int16 */
	const size_t cap = s_bufHalf;    /* 这块缓冲一共能装多少个 int16 */
	int fails = 0;                   /* 连续解码失败次数，防止死循环 */

	while (total < cap)              /* 一直填到装满为止 */
	{
		/* --- 步骤 1：先把上次剩的零头倒出去 ---
		 * MP3 一帧是 1152 个采样，缓冲块的大小不一定是它的整数倍，
		 * 所以每次解码完可能还剩一点没写进去。 */
		if (s_pendingPos < s_pendingCount)
		{
			size_t n = s_pendingCount - s_pendingPos;   /* 还剩多少 */
			if (n > cap - total) n = cap - total;       /* 但别超过这块的容量 */
			memcpy(out + total, s_pending + s_pendingPos, n * sizeof(int16_t));
			s_pendingPos += n;
			total += n;
			continue;                                   /* 回到 while 判断 */
		}

		/* --- 步骤 2：如果文件解到头了，就回到开头（循环播放）--- */
		if (s_mp3Offset >= s_mp3Size)
		{
			s_mp3Offset = 0;
			mp3dec_init(&s_dec);       /* 解码器状态也要复位 */
		}

		/* --- 步骤 3：解一帧 MP3 ---
		 * mp3dec_decode_frame 的参数：
		 *   &s_dec               解码器
		 *   s_mp3 + offset       从哪个字节开始
		 *   size - offset        还剩多少字节
		 *   s_pending            解出来的 PCM 写到哪
		 *   &info                顺带告诉这帧的信息
		 * 返回值 n = 这帧有多少个采样（每声道）。
		 * info.frame_bytes = 这帧在文件里占了多少字节。
		 */
		mp3dec_frame_info_t info;
		int n = mp3dec_decode_frame(&s_dec, s_mp3 + s_mp3Offset,
		                            (int)(s_mp3Size - s_mp3Offset),
		                            s_pending, &info);

		if (info.frame_bytes <= 0)
		{
			/* 找不到同步头。可能是文件损坏或者有杂数据，
			 * 往后挪一个字节再试；连续失败太多次就放弃。 */
			if (++fails > 8) break;
			s_mp3Offset += 1;
			continue;
		}
		fails = 0;
		s_mp3Offset += info.frame_bytes;   /* 前进这一帧的字节数 */

		if (n > 0)
		{
			/* 一帧解出来的 int16 个数 = 采样数 × 声道数 */
			s_pendingCount = (size_t)n * info.channels;
			s_pendingPos = 0;
			s_decodedSamples += n;         /* 累计，用来显示播放进度 */
		}
		else
		{
			s_pendingCount = s_pendingPos = 0;   /* 这帧没有音频（比如标记帧） */
		}
	}

	if (total == 0) return false;

	/* ==========================================================================
	 * 【整个项目最大的一个坑，务必看完再改】
	 * ==========================================================================
	 *
	 * ndspWaveBuf.nsamples 的单位是【帧数】（也就是每个声道的采样个数），
	 * 不是 int16 的总个数！
	 *
	 * 但 libctru 的头文件里注释写的是：
	 *     u32 nsamples;  ///< Total number of samples (PCM16=halfwords, ...)
	 * 这句话会把人带沟里。
	 *
	 * 更坑的是 devkitPro 自己的两个官方示例写法互相矛盾：
	 *     audio/streaming     → nsamples = 帧数          ✅ 正确
	 *     audio/ogg-vorbis    → nsamples = int16 总个数  ❌ 会出问题
	 *
	 * 如果按 int16 总个数填（也就是传了 2 倍的值），DSP 会以为每块数据
	 * 有双倍的时长，于是：【播放速度变成一半，听感一顿一顿的】。
	 *
	 * 症状很好认：解码本身很快，缓冲也从不空，但「已解码时长 / 运行时长」
	 * 永远稳定在 0.50 倍。以后遇到 3DS 音频卡顿，先来查这一行。
	 */
	wb->nsamples = total / (size_t)s_channels;   /* ← 除以声道数才是帧数 */

	ndspChnWaveBufAdd(AUDIO_CHN, wb);            /* 交给 DSP 排队播放 */

	/* 3DS 的 CPU 有数据缓存（cache）。DSP 是另一块硬件，它读内存时
	 * 不经过 CPU 的缓存，所以我们必须手动把这块数据从缓存刷到真实内存，
	 * 否则 DSP 可能读到旧数据 —— 听起来就是杂音或者卡顿。 */
	DSP_FlushDataCache(wb->data_pcm16, total * sizeof(int16_t));

	return true;
}

/* ---------------------------------------------------------------------------
 *  3.3  DSP 的回调函数
 * ---------------------------------------------------------------------------
 *  这个函数由系统在「DSP 播完一帧」时自动调用，大约每 4.9 毫秒一次。
 *
 *  这里绝对不要做耗时的活（比如解码）—— 它跑在一个很紧的时间限制里。
 *  它只做一件事：把事件设成「有信号」，叫醒正在等的音频线程。
 */
static void audioCallback(void *unused)
{
	(void)unused;      /* 参数没用到，加这行是为了消除编译警告 */
	if (!s_quit) LightEvent_Signal(&s_event);
}

/* ---------------------------------------------------------------------------
 *  3.4  音频线程本体
 * ---------------------------------------------------------------------------
 */
static void audioThread(void *unused)
{
	(void)unused;

	while (!s_quit)                       /* 主循环：一直干到有人叫停 */
	{
		/* 有人按了「重播」：把一切恢复到刚开机的状态 */
		if (s_restart)
		{
			s_restart = false;
			s_mp3Offset = 0;
			s_pendingCount = s_pendingPos = 0;
			mp3dec_init(&s_dec);
			s_decodedSamples = 0;
			ndspChnWaveBufClear(AUDIO_CHN);          /* 清空 DSP 里的待播队列 */
			for (int i = 0; i < NBUF; i++)
				s_waveBufs[i].status = NDSP_WBUF_DONE;  /* 标记成「空着，可以填」 */
			s_firstRun = true;
		}

		/* 找一块状态是 DONE（播完了/空着）的缓冲来填。
		 *
		 * s_firstRun 那一下是为了「开机先把垫子堆满」：刚开始播放时
		 * DSP 队列是空的，一次填满 4 块才够安全。之后每次只填一块，
		 * 把解码的 CPU 占用摊匀，避免一下子卡住好几毫秒。 */
		int allow = s_firstRun ? NBUF : 1;
		for (int i = 0; i < NBUF && allow > 0; i++)
		{
			if (s_waveBufs[i].status != NDSP_WBUF_DONE) continue;
			fillBuffer(&s_waveBufs[i]);
			allow--;
		}
		s_firstRun = false;

		/* 干活干完了，睡到下次 DSP 回调叫我们。
		 * 3DS 是协作式调度，不主动睡的话会霸占 CPU 不放。 */
		LightEvent_Wait(&s_event);
	}
}

/* ---------------------------------------------------------------------------
 *  3.5  音频初始化
 * ---------------------------------------------------------------------------
 *  顺序很重要：读文件 → 探测格式 → 配置 NDSP → 分配内存 → 起线程。
 */
static bool audioInit(void)
{
	/* --- 第 1 步：把整个 MP3 读进内存 ---
	 * "romfs:/" 是打包进 .3dsx 的资源目录，用法和普通文件路径一样。 */
	FILE *f = fopen("romfs:/music.mp3", "rb");
	if (!f) { s_audioErr = "cannot open romfs:/music.mp3"; return false; }

	fseek(f, 0, SEEK_END);            /* 跳到文件末尾 */
	long sz = ftell(f);               /* 问一下「现在在第几个字节」= 文件大小 */
	fseek(f, 0, SEEK_SET);            /* 跳回开头 */
	if (sz <= 0) { fclose(f); s_audioErr = "music.mp3 is empty"; return false; }

	/* 申请一块内存放文件内容。malloc 从普通堆里给，
	 * 这里不直接喂给 DSP，所以用 malloc 就行。 */
	s_mp3 = (uint8_t *)malloc((size_t)sz);
	if (!s_mp3) { fclose(f); s_audioErr = "out of memory"; return false; }

	if (fread(s_mp3, 1, (size_t)sz, f) != (size_t)sz)
	{ fclose(f); s_audioErr = "read failed"; return false; }
	fclose(f);
	s_mp3Size = (size_t)sz;

	/* --- 第 2 步：先解一帧，问出这个 MP3 的真实格式 ---
	 * 采样率和声道数不能写死：不同的 MP3 不一样，
	 * 必须让解码器告诉我们，NDSP 才能按正确的速率播放。 */
	mp3dec_init(&s_dec);
	mp3dec_frame_info_t info;
	int n = mp3dec_decode_frame(&s_dec, s_mp3, (int)s_mp3Size, s_pending, &info);
	if (n <= 0 || info.hz <= 0) { s_audioErr = "not a valid MP3"; return false; }
	s_hz       = info.hz;        /* 例如 44100 */
	s_channels = info.channels;  /* 1 或 2 */

	/* 探测完了，把状态复位，从头正式解码 */
	mp3dec_init(&s_dec);
	s_mp3Offset = 0;
	s_pendingCount = s_pendingPos = 0;
	s_decodedSamples = 0;

	/* --- 第 3 步：配置 NDSP 声道 --- */
	ndspChnReset(AUDIO_CHN);                                   /* 先复位 */
	ndspSetOutputMode(NDSP_OUTPUT_STEREO);                     /* 立体声输出 */
	ndspChnSetInterp(AUDIO_CHN, NDSP_INTERP_POLYPHASE);        /* 高质量重采样 */
	ndspChnSetRate(AUDIO_CHN, (float)s_hz);                    /* 告诉它源数据的采样率 */
	ndspChnSetFormat(AUDIO_CHN, s_channels == 1
	                            ? NDSP_FORMAT_MONO_PCM16       /* 单声道 */
	                            : NDSP_FORMAT_STEREO_PCM16);   /* 立体声 */

	/* --- 第 4 步：分配缓冲内存 ---
	 *
	 * 这里是本项目唯一必须用 linearAlloc 的地方，原因有两个：
	 *   ① DMA 要求：DSP 会自己去读这块内存，需要物理地址连续的区块。
	 *      malloc 出来的内存是虚拟地址连续，物理上不一定连续。
	 *   ② 地址转换：libctru 内部要把指针转成物理地址交给 DSP，
	 *      这个转换只对 linearAlloc 这种特殊区域有效。
	 *
	 * 每块 120 毫秒，4 块 = 总共 480 毫秒的预读深度。
	 * 深度越大越不怕卡顿，但音量/暂停的响应也会越迟钝。 */
	const size_t SAMPLES_PER_BUF = (size_t)s_hz * 120 / 1000;   /* 每块的帧数 */
	const size_t WAVEBUF_SIZE    = SAMPLES_PER_BUF * (size_t)s_channels * sizeof(int16_t);
	s_bufFrames = SAMPLES_PER_BUF;
	s_bufHalf   = SAMPLES_PER_BUF * (size_t)s_channels;

	s_audioBuffer = (int16_t *)linearAlloc(WAVEBUF_SIZE * NBUF);
	if (!s_audioBuffer) { s_audioErr = "linearAlloc failed"; return false; }

	/* 把 4 块缓冲「切」出来，填好各自的描述符 */
	memset(s_waveBufs, 0, sizeof(s_waveBufs));
	int16_t *p = s_audioBuffer;
	for (int i = 0; i < NBUF; i++)
	{
		s_waveBufs[i].data_vaddr = p;                    /* 数据在这 */
		s_waveBufs[i].nsamples   = SAMPLES_PER_BUF;      /* 长度用【帧数】，见 fillBuffer 的说明 */
		s_waveBufs[i].status     = NDSP_WBUF_DONE;       /* 一开始都是空的 */
		p += WAVEBUF_SIZE / sizeof(int16_t);             /* 挪到下一块 */
	}

	/* --- 第 5 步：装好回调、起线程 --- */
	LightEvent_Init(&s_event, RESET_ONESHOT);
	ndspSetCallback(audioCallback, NULL);
	audioApplyVolume();

	/* 线程优先级：数字越小优先级越高。
	 * 先问当前线程（也就是主线程）的优先级，然后比它高一级，
	 * 但夹在 0x18 ~ 0x3F 这个合法范围内。
	 * 让音频比画面优先级高一点，是「宁掉帧、不掉音」的常用做法。 */
	int32_t prio = 0x30;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	prio = (prio > 0x18) ? prio - 1 : 0x18;

	/* threadCreate(入口函数, 参数, 栈大小, 优先级, CPU核心(-1=随便), 是否立刻运行) */
	Thread th = threadCreate(audioThread, NULL, 32 * 1024, prio, -1, false);
	if (!th) { s_audioErr = "threadCreate failed"; return false; }

	s_audioOk = true;
	return true;
}

/* ---------------------------------------------------------------------------
 *  3.6  音频清理
 * ---------------------------------------------------------------------------
 *  程序退出前必须做的收尾，否则真机上可能黑屏或者卡在退出画面。
 */
static void audioExit(void)
{
	if (!s_audioOk) return;
	s_quit = true;                     /* 告诉音频线程「别干了」 */
	LightEvent_Signal(&s_event);       /* 把它从等待里叫醒 */
	ndspChnReset(AUDIO_CHN);
	if (s_audioBuffer) linearFree(s_audioBuffer);   /* linearAlloc 要用 linearFree 还 */
}


/* ============================================================================
 *  第 4 部分：画字的辅助函数
 * ============================================================================
 *
 *  用 citro2d 画一行字分三步，看起来很繁琐，所以包成一个函数：
 *
 *    C2D_TextFontParse  → 把字符串里的字符「登记」到文字缓存 g_buf 里
 *    C2D_TextOptimize   → 整理一下，让之后画得更快
 *    C2D_DrawText       → 真正画到当前画布上
 *
 *  g_buf 是「字形缓存」：citro2d 会按需把用到的字从字体文件里取出来裁成图片、
 *  拼进一张纹理。缓存有容量上限，满了就不会再收新字形。
 *
 *  参数：
 *    s        要画的字符串
 *    x, y     位置（像素）
 *    scale    缩放，1.0 是字体的原始大小
 *    color    颜色
 *    flags    对齐方式等，比如 C2D_AlignCenter 表示 x 是中心而不是左端
 */
static void drawText(const char *s, float x, float y, float scale, u32 color, u32 flags)
{
	C2D_Text t;
	C2D_TextFontParse(&t, g_font, g_buf, s);
	C2D_TextOptimize(&t);
	/* 最后一个参数的含义取决于 flags：带了 C2D_WithColor 时它就是颜色。
	 * 这里 flags 里一定带上 C2D_WithColor，所以最后传 color。 */
	C2D_DrawText(&t, flags | C2D_WithColor, x, y, 0.5f, scale, scale, color);
}


/* ============================================================================
 *  第 5 部分：下屏的触摸按钮
 * ============================================================================
 */

/* 一个按钮就是一个矩形，外加显示在上面的文字。 */
typedef struct {
	float x, y, w, h;      /* 左上角坐标 + 宽高 */
	const char *label;     /* 按钮文字 */
} Btn;

/* 4 个按钮摆成 2 × 2。下屏是 320 × 240，所以左右各留 18/168 的位置。 */
static const Btn g_btns[4] = {
	{  18.0f,  36.0f, 134.0f, 58.0f, "Play / Pause" },
	{ 168.0f,  36.0f, 134.0f, 58.0f, "Restart"      },
	{  18.0f, 102.0f, 134.0f, 58.0f, "Vol -"        },
	{ 168.0f, 102.0f, 134.0f, 58.0f, "Vol +"        },
};

/* 判断一个触摸点是不是落在某个按钮里面。
 * 触摸屏给的坐标就是下屏坐标（左上角原点），和按钮坐标是同一套。 */
static bool inBtn(const Btn *b, const touchPosition *tp)
{
	return tp->px >= b->x && tp->px < b->x + b->w &&
	       tp->py >= b->y && tp->py < b->y + b->h;
}


/* ============================================================================
 *  第 6 部分：main —— 程序从这里开始跑
 * ============================================================================
 */
int main(int argc, char **argv)
{
	/* 程序不需要命令行参数，这两行是消除「参数没用」的编译警告。 */
	(void)argc; (void)argv;

	/* ---------------------------------------------------------------------
	 *  6.1  初始化所有子系统
	 * ---------------------------------------------------------------------
	 *  顺序不能乱：先底层后上层。
	 *  退出时的顺序要反过来（见本函数最后）。
	 */
	gfxInitDefault();                      /* 屏幕/帧缓冲的底层支持 */
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);     /* citro3d：GPU 命令缓冲 */
	C2D_Init(C2D_DEFAULT_MAX_OBJECTS);     /* citro2d：2D 绘制器 */
	C2D_Prepare();                         /* 告诉 GPU 可以开始接收 2D 绘制了 */
	romfsInit();                           /* 挂载打包在 .3dsx 里的 romfs 资源目录 */

	/* 给两个屏幕各创建一块画布 */
	g_top = C2D_CreateScreenTarget(GFX_TOP,    GFX_LEFT);
	g_bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

	/* 文字缓存，4096 个字形。屏幕上就那么几个字，够用很久。 */
	g_buf = C2D_TextBufNew(4096);

	/* 载入中文字体。
	 * 3DS 自带的系统字体在欧美版机器上没有中日韩汉字，所以在模拟器和
	 * 大部分真机上都不能指望它。正确做法是把自己的字体打包进 romfs：
	 * 用 mkbcfnt 把 TTF/OTF 转成 .bcfnt，可以只取用到的字，体积很小。
	 * 详细做法见 README。
	 *
	 * 加载失败时返回 NULL，下面的代码会优雅降级，不会崩。 */
	g_font = C2D_FontLoad("romfs:/cjk.bcfnt");

	/* 载入 tex3ds 转好的图片包，取出第一张图。
	 * 同样做失败保护。 */
	g_logoSheet = C2D_SpriteSheetLoad("romfs:/gfx/logo.t3x");
	if (g_logoSheet) g_logo = C2D_SpriteSheetGetImage(g_logoSheet, 0);

	/* ---------------------------------------------------------------------
	 *  6.2  初始化音频
	 * ---------------------------------------------------------------------
	 *  ndspInit() 需要 DSP 固件文件 sdmc:/3ds/dspfirm.cdc，该文件按许可不可随项目
	 *  分发，需要使用者自行准备。没有它就会失败 —— 但失败不代表程序不能用，
	 *  只是没声音，所以这里不退出，而是把原因记下来显示在屏幕上。 */
	Result ndspRes = ndspInit();
	bool ndspOk = R_SUCCEEDED(ndspRes);
	if (ndspOk)
	{
		if (!audioInit()) s_audioErr = "audioInit failed";
	}
	else
	{
		/* R_FAILED / R_SUCCEEDED 是 libctru 的判断宏。
		 * 错误码 0xD880A7FA 表示「找不到 DSP 组件」，也就是缺固件。 */
		snprintf(s_ndspMsg, sizeof(s_ndspMsg), "ndspInit failed 0x%08lX",
		         (unsigned long)ndspRes);
		s_audioErr = s_ndspMsg;
	}

	/* ---------------------------------------------------------------------
	 *  6.3  主循环
	 * ---------------------------------------------------------------------
	 *  游戏/应用的主循环长这样：
	 *      读输入 → 更新状态 → 画一帧 → 重复
	 *
	 *  aptMainLoop() 每次迭代会检查「系统有没有要求退出」（比如用户
	 *  按了 Home 键、或者合盖休眠）。它返回 false 就该退出了。
	 *  这个判断绝对不能省，否则按 Home 键会死机。 */
	char line[160];

	while (aptMainLoop())
	{
		/* --- 读输入 ---
		 * hidScanInput() 把这一刻的按键状态刷进内存，后面才能查询。 */
		hidScanInput();

		/* hidKeysDown()：这一帧刚刚按下的（按住的瞬间只会有一次）
		 * hidKeysHeld()：这一帧正被按住的
		 * hidTouchRead()：触摸屏的坐标，没触摸时 px/py 是 0 */
		u32 kDown = hidKeysDown();
		u32 kHeld = hidKeysHeld();
		touchPosition tp;
		hidTouchRead(&tp);

		/* START 键退出 */
		if (kDown & KEY_START) break;

		/* --- 处理触摸按钮 ---
		 * 只在「刚按下」的那一帧响应（kDown），
		 * 不然按住不放会一秒触发几十次。 */
		if (kDown & KEY_TOUCH)
		{
			for (int i = 0; i < 4; i++)
			{
				if (!inBtn(&g_btns[i], &tp)) continue;
				switch (i)
				{
				case 0:   /* 播放 / 暂停 */
					s_playing = !s_playing;
					ndspChnSetPaused(AUDIO_CHN, !s_playing);
					break;
				case 1:   /* 重播：让音频线程去复位 */
					s_restart = true;
					s_playing = true;
					ndspChnSetPaused(AUDIO_CHN, false);
					break;
				case 2:   /* 音量 - */
					s_volume -= 0.1f;
					if (s_volume < 0.0f) s_volume = 0.0f;
					audioApplyVolume();
					break;
				case 3:   /* 音量 + */
					s_volume += 0.1f;
					if (s_volume > 1.0f) s_volume = 1.0f;
					audioApplyVolume();
					break;
				}
			}
		}

		/* --- 收集现在按着的物理按键，拼成一行字 --- */
		static const struct { u32 k; const char *n; } KEYS[] = {
			{ KEY_A, "A" }, { KEY_B, "B" }, { KEY_X, "X" }, { KEY_Y, "Y" },
			{ KEY_L, "L" }, { KEY_R, "R" }, { KEY_UP, "UP" }, { KEY_DOWN, "DN" },
			{ KEY_LEFT, "LF" }, { KEY_RIGHT, "RT" }, { KEY_SELECT, "SEL" },
		};
		char held[64] = "";
		for (unsigned i = 0; i < ARRAY_SIZE(KEYS); i++)
		{
			if (!(kHeld & KEYS[i].k)) continue;
			size_t n = strlen(held);
			snprintf(held + n, sizeof(held) - n, "%s ", KEYS[i].n);
		}

		/* -----------------------------------------------------------------
		 *  6.4  画这一帧
		 * -----------------------------------------------------------------
		 */
		/* 清空文字缓存。注意：必须每帧清！
		 * 因为每帧都会重新 Parse 一次字符串，不清的话缓存很快就会被撑爆，
		 * 之后新字形就登记不进去了（表现为文字缺字）。 */
		C2D_TextBufClear(g_buf);

		/* 开始记录这一帧的 GPU 命令。所有绘制调用都要夹在
		 * C3D_FrameBegin 和 C3D_FrameEnd 之间。 */
		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

		/* ---------- 上屏 ---------- */

		/* 选画布 + 清成某个底色 */
		C2D_TargetClear(g_top, CLR_BG);
		C2D_SceneBegin(g_top);

		/* 画图片。
		 * C2D_DrawImageAt(图, x, y, 图层深度, 染色(NULL=不染色), 横向缩放, 纵向缩放)
		 * 这张图本身是 200×200，缩小到 0.62 倍。 */
		if (g_logoSheet)
			C2D_DrawImageAt(g_logo, 14.0f, 26.0f, 0.5f, NULL, 0.62f, 0.62f);

		/* 标题 */
		drawText("Hello, World", 178.0f, 24.0f, 0.85f, CLR_TEXT, 0);
		drawText("My first Nintendo 3DS homebrew", 178.0f, 68.0f, 0.42f, CLR_ACCENT, 0);

		/* 中文字体演示：只有字体加载成功时才画。
		 * 这一行证明了打包进去的 CJK 字体确实可用。 */
		if (g_font)
			drawText("CJK demo: 你好，世界", 178.0f, 92.0f, 0.38f, CLR_DIM, 0);

		/* 音乐状态 */
		if (s_audioOk)
		{
			/* s_decodedSamples 会被音频线程随时改写。u64 在 32 位 CPU 上
			 * 不是一次写完的，主线程有可能读到「半新半旧」的值（撕裂读）。
			 * 连续读两次，两次一样就认为读到的是完整值。 */
			u64 a, b;
			do { a = s_decodedSamples; b = s_decodedSamples; } while (a != b);

			u64 secs = a / (u64)(s_hz ? s_hz : 44100);

			snprintf(line, sizeof(line), "Music: %s",
			         s_playing ? "playing" : "paused");
			drawText(line, 178.0f, 120.0f, 0.42f, CLR_DIM, 0);

			snprintf(line, sizeof(line), "Volume %d%%   Position %02llu:%02llu",
			         (int)(s_volume * 100.0f + 0.5f),
			         (unsigned long long)(secs / 60),
			         (unsigned long long)(secs % 60));
			drawText(line, 178.0f, 140.0f, 0.38f, CLR_DIM, 0);
		}
		else
		{
			/* 音频起不来时，把原因显示出来，方便排查 */
			snprintf(line, sizeof(line), "Audio unavailable: %s", s_audioErr);
			drawText(line, 178.0f, 120.0f, 0.34f, CLR_DIM, 0);
		}

		/* 当前按着哪些物理按键 */
		snprintf(line, sizeof(line), "Buttons: %s", held[0] ? held : "-");
		drawText(line, 178.0f, 166.0f, 0.40f, CLR_TEXT, 0);

		/* 底部提示。C2D_AlignCenter 表示第一个参数 x 是「中心点」而不是左端。 */
		drawText("Use the touch screen below  ·  START to quit",
		          TOP_W * 0.5f, 214.0f, 0.42f, CLR_DIM, C2D_AlignCenter);

		/* ---------- 下屏 ---------- */

		C2D_TargetClear(g_bot, CLR_BG2);
		C2D_SceneBegin(g_bot);

		drawText("Touch buttons", BOT_W * 0.5f, 8.0f, 0.5f, CLR_ACCENT, C2D_AlignCenter);

		for (int i = 0; i < 4; i++)
		{
			const Btn *b = &g_btns[i];

			/* 手指正按在这个按钮上时，把它点亮。
			 * 数字 0.4 是图层深度，越小的越靠前，这里保证文字画在按钮上面。 */
			bool on = (kHeld & KEY_TOUCH) && inBtn(b, &tp);
			C2D_DrawRectSolid(b->x, b->y, 0.4f, b->w, b->h,
			                  on ? CLR_BTN_ON : CLR_BTN);

			/* 文字居中：x 取矩形中心，y 取中心再往上挪半个字高 */
			drawText(b->label,
			         b->x + b->w * 0.5f, b->y + b->h * 0.5f - 9.0f,
			         0.5f, on ? CLR_BG : CLR_TEXT, C2D_AlignCenter);
		}

		snprintf(line, sizeof(line), "Volume %d%%    %s",
		         (int)(s_volume * 100.0f + 0.5f),
		         s_audioOk ? (s_playing ? "playing" : "paused") : "no audio");
		drawText(line, BOT_W * 0.5f, 172.0f, 0.46f, CLR_TEXT, C2D_AlignCenter);

		if (kHeld & KEY_TOUCH)
			snprintf(line, sizeof(line), "Touch (%d, %d)", (int)tp.px, (int)tp.py);
		else
			snprintf(line, sizeof(line), "Tap a button to try it");
		drawText(line, BOT_W * 0.5f, 196.0f, 0.42f, CLR_DIM, C2D_AlignCenter);

		if (s_audioOk)
			snprintf(line, sizeof(line), "%d Hz  ·  %s", s_hz,
			         s_channels == 1 ? "mono" : "stereo");
		else
			snprintf(line, sizeof(line), "audio unavailable");
		drawText(line, BOT_W * 0.5f, 218.0f, 0.36f, CLR_DIM, C2D_AlignCenter);

		/* 结束这一帧，把画好的内容真正送到屏幕上。
		 * 参数 0 表示「等 GPU 画完再继续」。 */
		C3D_FrameEnd(0);
	}

	/* ---------------------------------------------------------------------
	 *  6.5  收尾
	 * ---------------------------------------------------------------------
	 *  初始化的顺序是「从下往上」，清理就要「从上往下」，一一对应。
	 */
	if (g_font)       C2D_FontFree(g_font);
	C2D_TextBufDelete(g_buf);
	if (g_logoSheet)  C2D_SpriteSheetFree(g_logoSheet);
	C2D_Fini();
	C3D_Fini();
	if (ndspOk) { audioExit(); ndspExit(); }
	free(s_mp3);
	romfsExit();
	gfxExit();
	return 0;
}
