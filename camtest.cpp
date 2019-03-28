#include <iostream>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/types.h>
#include <linux/fb.h>
#include <linux/videodev2.h>
#include <sys/poll.h>
#include <arpa/inet.h>
#include "videodev2_samsung.h"
#include "bmp_head.h"

#define CAMERA_DEV_NAME   "/dev/video0"

class TError {
public:
	TError(const char *msg) {
		this->msg = msg;
	}
	TError(const TError bitand e) {
		msg = e.msg;
	}
	void Output() {
		std::cerr << msg << std::endl;
	}
	virtual ~TError() {}
protected:
	TError bitand operator=(const TError bitand);
private:
	const char *msg;
};

// Linear memory based image
class TRect {
public:
	TRect():  Addr(0), Size(0), Width(0), Height(0), LineLen(0), BPP(32) {
	}
	virtual ~TRect() {
	}
	bool DrawRect(const TRect bitand SrcRect, int x, int y) const {
		if (BPP not_eq 32 or SrcRect.BPP not_eq 32) {
			// don't support that yet
			throw TError("does not support other than 32 BPP yet");
		}

		// clip
		int x0, y0, x1, y1;
		x0 = x;
		y0 = y;
		x1 = x0 + SrcRect.Width - 1;
		y1 = y0 + SrcRect.Height - 1;
		if (x0 < 0) {
			x0 = 0;
		}
		if (x0 > Width - 1) {
			return true;
		}
		if( x1 < 0) {
			return true;
		}
		if (x1 > Width - 1) {
			x1 = Width - 1;
		}
		if (y0 < 0) {
			y0 = 0;
		}
		if (y0 > Height - 1) {
			return true;
		}
		if (y1 < 0) {
			return true;
		}
		if (y1 > Height - 1) {
			y1 = Height -1;
		}

		//copy
		int copyLineLen = (x1 + 1 - x0) * BPP / 8;
		unsigned char *DstPtr = Addr + LineLen * y0 + x0 * BPP / 8;
		const unsigned char *SrcPtr = SrcRect.Addr + SrcRect.LineLen *(y0 - y) + (x0 - x) * SrcRect.BPP / 8;

		for (int i = y0; i <= y1; i++) {
			memcpy(DstPtr, SrcPtr, copyLineLen);
			DstPtr += LineLen;
			SrcPtr += SrcRect.LineLen;
		}
		
		
		return true;
	}

	bool DrawRect(const TRect bitand rect) const { // default is Center
		return DrawRect(rect, (Width - rect.Width) / 2, (Height - rect.Height) / 2);
	}

	bool Clear() const {
		int i;
		unsigned char *ptr;
		for (i = 0, ptr = Addr; i < Height; i++, ptr += LineLen) {
			memset(ptr, 0, Width * BPP / 8);
		}
		return true;
	}



protected:
	TRect(const TRect bitand);
	TRect bitand operator=( const TRect bitand);

public:
	unsigned char *Addr;
	int Size;
	int Width, Height, LineLen;
	unsigned BPP;
};



class TFrameBuffer: public TRect {
public:
	TFrameBuffer(const char *DeviceName = "/dev/fb0"): TRect(), fd(-1) {
		Addr = (unsigned char *)MAP_FAILED;

        fd = open(DeviceName, O_RDWR);
		if (fd < 0) {
			throw TError("cannot open frame buffer");
		}

        struct fb_fix_screeninfo Fix;
        struct fb_var_screeninfo Var;
		if (ioctl(fd, FBIOGET_FSCREENINFO, bitand Fix) < 0 or ioctl(fd, FBIOGET_VSCREENINFO, bitand Var) < 0) {
			throw TError("cannot get frame buffer information");
		}

		BPP = Var.bits_per_pixel;
	    if (BPP not_eq 32) {
			throw TError("support 32 BPP frame buffer only");
		}

        	Width  = Var.xres;
        	Height = Var.yres;
        	LineLen = Fix.line_length;
      		Size = LineLen * Height;

		int PageSize = getpagesize();
		Size = (Size + PageSize - 1) / PageSize * PageSize ;
	        Addr = (unsigned char *)mmap(NULL, Size, PROT_READ|PROT_WRITE,MAP_SHARED, fd, 0);
		if (Addr == (unsigned char *)MAP_FAILED) {
			throw TError("map frame buffer failed");
			return;
		}

		::close(fd);
		fd = -1;

		Clear();
	}

	virtual ~TFrameBuffer() {
		::munmap(Addr, Size);
		Addr = (unsigned char *)MAP_FAILED;

		::close(fd);
		fd = -1;
	}

	unsigned char* getAddr() {
		return Addr;
	}
	

protected:
	TFrameBuffer(const TFrameBuffer bitand);
	TFrameBuffer bitand operator=( const TFrameBuffer bitand);
private:
	int fd;
};

class TVideo : public TRect {
public:
	TVideo(const char *DeviceName = CAMERA_DEV_NAME): TRect(), fd(-1) {
		Width = 640;
		Height = 480;
		BPP = 32;
		LineLen = Width * BPP / 8;
		Size = LineLen * Height;
		fd = -1;
        Valid = true;
		Addr = new unsigned char[Size];
		Clear();

        OpenDevice();
        StartStream();
	}
    
    bool IsValid() const { return Valid; }
    bool WaitPic();
	bool FetchPicture();

	virtual ~TVideo() {
		::close(fd);
		fd = -1;
		delete[] Addr;
		Addr = 0;
	}

protected:
	TVideo(const TVideo bitand);
	TVideo bitand operator=(const TVideo bitand);
    void OpenDevice();
    void StartStream();
    void StopStream();

	int fd;
    bool Valid;
    struct pollfd  m_events_c;
	static const int CAPTURE_BUFFER_NUMBER = 1;
	struct { void * data; int len; } captureBuffer[CAPTURE_BUFFER_NUMBER];
	
};

void TVideo::OpenDevice()
{
	// Open Device
	const char *device = CAMERA_DEV_NAME;
	fd = ::open(device, O_RDWR|O_NONBLOCK);
	if (fd < 0) {
        Valid = false;
		fprintf(stderr, "cannot open device %s\n", device);
		return;
	}
    
	// Check capability
	struct v4l2_capability cap;
	if( ::ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        Valid = false;
		fprintf(stderr, "cannot query capability\n");
		return;
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        Valid = false;
		fprintf(stderr, "not a video capture device\n");
		return;
	}
    
	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        Valid = false;
		fprintf(stderr, "does not support streaming i/o\n");
		return;
	}
    
    static struct v4l2_input input;
    input.index = 0; 
    if (ioctl(fd, VIDIOC_ENUMINPUT, &input) != 0) {
        Valid = false;
        fprintf(stderr, "No matching index found\n");
        return;
    }
    if (!input.name) {
        Valid = false;
        fprintf(stderr, "No matching index found\n");
        return;
    }
    if (ioctl(fd, VIDIOC_S_INPUT, &input) < 0) {
        Valid = false;
        fprintf(stderr, "VIDIOC_S_INPUT failed\n");
        return;
    }
    
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof fmt);
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = Width;
    fmt.fmt.pix.height      = Height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_NV21;
    fmt.fmt.pix.sizeimage = (fmt.fmt.pix.width * fmt.fmt.pix.height * 16) / 8;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (::ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        Valid = false;
        fprintf(stderr, "VIDIOC_S_FMT failed\n");
        return;
    }
    
	bool CouldSetFrameRate = false;
	struct v4l2_streamparm StreamParam;
	memset(&StreamParam, 0, sizeof StreamParam);
	StreamParam.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(fd, VIDIOC_G_PARM, &StreamParam) < 0)  {
        fprintf(stderr, "could not set frame rate\n");
	} else {
		CouldSetFrameRate = StreamParam.parm.capture.capability & V4L2_CAP_TIMEPERFRAME;
	}

    // map the capture buffer...
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof req);
    req.count  = CAPTURE_BUFFER_NUMBER;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if(ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        Valid = false;
    	fprintf(stderr, "request capture buffer failed\n");
    	return;
    }
    
    if (int(req.count) != CAPTURE_BUFFER_NUMBER) {
    	fprintf(stderr, "capture buffer number is wrong\n");
        Valid = false;
        return;
    }

    for (int i = 0; i < CAPTURE_BUFFER_NUMBER; i++) {
    	struct v4l2_buffer b;
    	memset(&b, 0, sizeof b);
    	b.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    	b.memory = V4L2_MEMORY_MMAP;
    	b.index = i;
    	if (ioctl(fd, VIDIOC_QUERYBUF, &b) < 0) {
            Valid = false;
    		fprintf(stderr, "query capture buffer failed\n");
    		return;
    	}
        
    	captureBuffer[i].data = mmap(0, b.length, PROT_READ|PROT_WRITE, MAP_SHARED, fd, b.m.offset);
    	captureBuffer[i].len = b.length;
        
    	if (captureBuffer[i].data == MAP_FAILED) {
            Valid = false;
    		fprintf(stderr, "unable to map capture buffer\n");
    		return;
    	}
        
        fprintf(stderr, "ImageSize[%d] = %ld\n", i, b.length);
    }

    if (Valid) {
        fprintf(stderr, "Open Device OK!\n");
    }

    struct v4l2_control ctrl;
    ctrl.id = V4L2_CID_CAMERA_CHECK_DATALINE;
    ctrl.value = 0;
    if(ioctl(fd, VIDIOC_S_CTRL,&ctrl)) {
        fprintf(stderr, "VIDIOC_S_CTRL V4L2_CID_CAMERA_CHECK_DATALINE failed\n");
        Valid = false;
        return;
    }

    memset(&m_events_c, 0, sizeof(m_events_c));
    m_events_c.fd = fd;
    m_events_c.events = POLLIN | POLLERR;
    
	return;
}


void TVideo::StartStream()
{
    for (int i = 0; i < CAPTURE_BUFFER_NUMBER; i++) {
        struct v4l2_buffer b;
        memset(&b, 0, sizeof b);
        b.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        b.index = i;

        if (ioctl (fd, VIDIOC_QBUF, &b) < 0) {
            Valid = false;
            fprintf(stderr, "queue capture failed\n");
            return;
        }
    }
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl (fd, VIDIOC_STREAMON, &type) < 0) {
        Valid = false;
        fprintf(stderr, "cannot start stream\n");
        return;
    }

    if (Valid) {
        fprintf(stderr, "StartStream OK!\n");
    }
}

void TVideo::StopStream()
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl (fd, VIDIOC_STREAMOFF, &type) < 0) {
        Valid = false;
        fprintf(stderr, "cannot stop stream\n");
        return;
    }
}

bool TVideo::WaitPic()
{
    int ret = poll(&m_events_c,  1, 10000);
    if (ret > 0) {
        return true;
    }
    return false;
}

static void decodeYUV420SP(unsigned int* rgbBuf, unsigned char* yuv420sp, int width, int height) {  
    int frameSize = width * height;  

    int i = 0, y = 0;  
    int uvp = 0, u = 0, v = 0;  
    int y1192 = 0, r = 0, g = 0, b = 0;  
    unsigned int xrgb8888;
    int xrgb8888Index = 0;

    for (int j = 0, yp = 0; j < height; j++) {  
        uvp = frameSize + (j >> 1) * width;  
        u = 0;  
        v = 0;  
        for (i = 0; i < width; i++, yp++) {  
            y = (0xff & ((int) yuv420sp[yp])) - 16;  
            if (y < 0) y = 0;  
            if ((i & 1) == 0) {  
                v = (0xff & yuv420sp[uvp++]) - 128;  
                u = (0xff & yuv420sp[uvp++]) - 128;  
            }  

            y1192 = 1192 * y;  
            r = (y1192 + 1634 * v);  
            g = (y1192 - 833 * v - 400 * u);  
            b = (y1192 + 2066 * u);  

            if (r < 0) r = 0; else if (r > 262143) r = 262143;  
            if (g < 0) g = 0; else if (g > 262143) g = 262143;  
            if (b < 0) b = 0; else if (b > 262143) b = 262143; 


            r = (unsigned char)(r >> 10);  
            g = (unsigned char)(g >> 10);  
            b = (unsigned char)(b >> 10); 

            xrgb8888 = (unsigned int)((r << 16) | (g << 8) | b);
            rgbBuf[xrgb8888Index++] = xrgb8888;
        }  
    }  
}

bool TVideo::FetchPicture()
{
	struct v4l2_buffer b;
	memset(&b, 0, sizeof b);
	b.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	b.memory = V4L2_MEMORY_MMAP;
	if (ioctl(fd, VIDIOC_DQBUF, &b) < 0) {
        Valid = false;
		fprintf(stderr, "cannot fetch picture(VIDIOC_DQBUF failed)\n");
		return false;
	}

    
    void *data_ = captureBuffer[b.index].data;
    unsigned int len = b.bytesused;
    unsigned int index = b.index;

    unsigned char* data = (unsigned char*) data_;

    decodeYUV420SP((unsigned int*)Addr, data, Width, Height);
    
	if (ioctl (fd, VIDIOC_QBUF, &b) < 0) {
        Valid = false;
		fprintf(stderr, "cannot fetch picture(VIDIOC_QBUF failed)\n");
		return false;
	}
    
    return true;
}

unsigned char *bufferMalloc;

int main(int argc, char **argv)
{
	try {
		TFrameBuffer FrameBuffer;
		TVideo Video;
        while(Video.IsValid()) {
            if (Video.WaitPic()) {
                if (Video.FetchPicture()) {
                    FrameBuffer.DrawRect(Video);
					unsigned char *buffer= Video.Addr;
					BmpHead bmpHead;
					InfoHead infoHead;
					bmpHead.type=0x4d42;
					bmpHead.imageSize=Video.Width*Video.Height*3+54;
					bmpHead.blank=0;
					bmpHead.startPosition =54;
					printf("%d\n",bmpHead.imageSize);
					printf("%d\n",bmpHead.blank);
					printf("%d\n",sizeof(bmpHead));

					infoHead.Length = 40;
					infoHead.width = Video.Width;
                	infoHead.height = Video.Height;
					infoHead.colorPlane = 1;
					infoHead.bitColor =24;
					infoHead.zipFormat = 0;
					infoHead.realSize = 0;
					infoHead.xPels = 0;
					infoHead.yPels = 0;
					infoHead.colorUse = 0;
					infoHead.colorImportant = 0;
					printf("%d\n",sizeof(infoHead));
					bufferMalloc=(unsigned char *)malloc(Video.Width*Video.Height*3+54);

					memcpy(bufferMalloc, &bmpHead, 14);
					
					memcpy(bufferMalloc+14, &infoHead, 40);
					
					memcpy(bufferMalloc+14+40, buffer, Video.Width*Video.Height*3);

					int fd_bmp;
					fd_bmp = open("./1.bmp", O_CREAT | O_WRONLY);
					if(fd_bmp == -1) {
						perror("open bmp");
        				return -1; 
					}

					write(fd_bmp, bufferMalloc, 800*400*3+54);
					close(fd_bmp);
					free(bufferMalloc);
					return 0;

				}
            }
			printf("t\n");
        }
	} catch (TError bitand e) {
		e.Output();
		return 1;
	}

	return 0;
}

