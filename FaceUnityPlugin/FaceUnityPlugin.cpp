//
//  FaceUnityPlugin.cpp
//  FaceUnityPlugin
//
//  Created by 张乾泽 on 2019/8/6.
//  Copyright © 2019 Agora Corp. All rights reserved.
//
#include "FaceUnityPlugin.h"
#include <string.h>
#include <string>
#include "funama.h"
#include "FUConfig.h"
#include "Utils.h"
#include <stdlib.h>
#include <iostream>
#include "common/rapidjson/document.h"
#ifdef _WIN32
#include "windows.h"
#pragma comment(lib, "nama.lib")
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "glu32.lib")
#else
#include <dlfcn.h>
#include <OpenGL/OpenGL.h>
#endif // WIN32


#define MAX_PATH 512

static bool mNamaInited = false;
static int mFrameID = 0;
static int mBeautyHandles = 0;

using namespace rapidjson;

#if defined(_WIN32)
PIXELFORMATDESCRIPTOR pfd = {
    sizeof(PIXELFORMATDESCRIPTOR),
    1u,
    PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER | PFD_DRAW_TO_WINDOW,
    PFD_TYPE_RGBA,
    32u,
    0u, 0u, 0u, 0u, 0u, 0u,
    8u,
    0u,
    0u,
    0u, 0u, 0u, 0u,
    24u,
    8u,
    0u,
    PFD_MAIN_PLANE,
    0u,
    0u, 0u };
#endif

FaceUnityPlugin::FaceUnityPlugin()
{
    
}

FaceUnityPlugin::~FaceUnityPlugin()
{
    
}

bool FaceUnityPlugin::initOpenGL()
{
#ifdef _WIN32
	HWND hw = CreateWindowExA(
		0, "EDIT", "", ES_READONLY,
		0, 0, 1, 1,
		NULL, NULL,
		GetModuleHandleA(NULL), NULL);
	HDC hgldc = GetDC(hw);
	int spf = ChoosePixelFormat(hgldc, &pfd);
	int ret = SetPixelFormat(hgldc, spf, &pfd);
	HGLRC hglrc = wglCreateContext(hgldc);
	wglMakeCurrent(hgldc, hglrc);

	//hglrc就是创建出的OpenGL context
	printf("hw=%08x hgldc=%08x spf=%d ret=%d hglrc=%08x\n",
		hw, hgldc, spf, ret, hglrc);
#else
	CGLPixelFormatAttribute attrib[13] = { kCGLPFAOpenGLProfile,
		(CGLPixelFormatAttribute)kCGLOGLPVersion_Legacy,
		kCGLPFAAccelerated,
		kCGLPFAColorSize, (CGLPixelFormatAttribute)24,
		kCGLPFAAlphaSize, (CGLPixelFormatAttribute)8,
		kCGLPFADoubleBuffer,
		kCGLPFASampleBuffers, (CGLPixelFormatAttribute)1,
		kCGLPFASamples, (CGLPixelFormatAttribute)4,
		(CGLPixelFormatAttribute)0 };
	CGLPixelFormatObj pixelFormat = NULL;
	GLint numPixelFormats = 0;
	CGLContextObj cglContext1 = NULL;
	CGLChoosePixelFormat(attrib, &pixelFormat, &numPixelFormats);
	CGLError err = CGLCreateContext(pixelFormat, NULL, &cglContext1);
	CGLSetCurrentContext(cglContext1);
	if (err) {
		return false;
	}
#endif
	return true;
}

unsigned char *FaceUnityPlugin::yuvData(VideoPluginFrame* videoFrame)
{
    int ysize = videoFrame->yStride * videoFrame->height;
    int usize = videoFrame->uStride * videoFrame->height / 2;
    int vsize = videoFrame->vStride * videoFrame->height / 2;
    unsigned char *temp = (unsigned char *)malloc(ysize + usize + vsize);
    
    memcpy(temp, videoFrame->yBuffer, ysize);
    memcpy(temp + ysize, videoFrame->uBuffer, usize);
    memcpy(temp + ysize + usize, videoFrame->vBuffer, vsize);
    return (unsigned char *)temp;
}

int FaceUnityPlugin::yuvSize(VideoPluginFrame* videoFrame)
{
    int ysize = videoFrame->yStride * videoFrame->height;
    int usize = videoFrame->uStride * videoFrame->height / 2;
    int vsize = videoFrame->vStride * videoFrame->height / 2;
    return ysize + usize + vsize;
}

void FaceUnityPlugin::videoFrameData(VideoPluginFrame* videoFrame, unsigned char *yuvData)
{
    int ysize = videoFrame->yStride * videoFrame->height;
    int usize = videoFrame->uStride * videoFrame->height / 2;
    int vsize = videoFrame->vStride * videoFrame->height / 2;
    
    memcpy(videoFrame->yBuffer, yuvData,  ysize);
    memcpy(videoFrame->uBuffer, yuvData + ysize, usize);
    memcpy(videoFrame->vBuffer, yuvData + ysize + usize, vsize);
}

bool FaceUnityPlugin::onPluginRenderVideoFrame(unsigned int uid, VideoPluginFrame *videoFrame)
{
    return true;
}

bool FaceUnityPlugin::onPluginCaptureVideoFrame(VideoPluginFrame *videoFrame)
{
    if(auth_package_size == 0){
        return false;
    }
    do {
        // 1. check if thread changed, if yes, reinit opengl
#if defined(_WIN32)
		int currentThreadId = GetCurrentThreadId();
		if (currentThreadId != videoFrameThreadId) {
			if (mNamaInited) {
				fuOnCameraChange();
				fuOnDeviceLost();
				fuDestroyAllItems();
				mNamaInited = false;
				mNeedUpdateFUOptions = true;
			}
		}
		videoFrameThreadId = currentThreadId;
#endif
        // 2. initialize if not yet done
        if (!mNamaInited) {
            //load nama and initialize
            std::string sub = "agora_node_ext.node";
            std::string assets_dir = folderPath + assets_dir_name + file_separator;
            std::string g_fuDataDir = assets_dir;
            std::vector<char> v3data;
            if (false == Utils::LoadBundle(g_fuDataDir + g_v3Data, v3data)) {
                break;
            }
            initOpenGL();
            //CheckGLContext();
            fuSetup(reinterpret_cast<float*>(&v3data[0]), v3data.size(), NULL, auth_package, auth_package_size);
            
            std::vector<char> propData;
            if (false == Utils::LoadBundle(g_fuDataDir + g_faceBeautification, propData)) {
                std::cout << "load face beautification data failed." << std::endl;
                break;
            }
            std::cout << "load face beautification data." << std::endl;
            
            mBeautyHandles = fuCreateItemFromPackage(&propData[0], propData.size());
            mNamaInited = true;
        }
        
        
        // 3. beauty params
        // check if options needs to be updated
        if (mNeedUpdateFUOptions) {
            fuItemSetParams(mBeautyHandles, "filter_name", const_cast<char*>(filter_name.c_str()));
            fuItemSetParamd(mBeautyHandles, "filter_level", filter_level);
            fuItemSetParamd(mBeautyHandles, "color_level", color_level);
            fuItemSetParamd(mBeautyHandles, "red_level", red_level);
            fuItemSetParamd(mBeautyHandles, "blur_level", blur_level);
            fuItemSetParamd(mBeautyHandles, "skin_detect", skin_detect);
            fuItemSetParamd(mBeautyHandles, "nonshin_blur_scale", nonshin_blur_scale);
            fuItemSetParamd(mBeautyHandles, "heavy_blur", heavy_blur);
            fuItemSetParamd(mBeautyHandles, "face_shape", face_shape);
            fuItemSetParamd(mBeautyHandles, "face_shape_level", face_shape_level);
            fuItemSetParamd(mBeautyHandles, "eye_enlarging", eye_enlarging);
            fuItemSetParamd(mBeautyHandles, "cheek_thinning", cheek_thinning);
            fuItemSetParamd(mBeautyHandles, "cheek_v", cheek_v);
            fuItemSetParamd(mBeautyHandles, "cheek_narrow", cheek_narrow);
            fuItemSetParamd(mBeautyHandles, "cheek_small", cheek_small);
            fuItemSetParamd(mBeautyHandles, "cheek_oval", cheek_oval);
            fuItemSetParamd(mBeautyHandles, "intensity_nose", intensity_nose);
            fuItemSetParamd(mBeautyHandles, "intensity_forehead", intensity_forehead);
            fuItemSetParamd(mBeautyHandles, "intensity_mouth", intensity_mouth);
            fuItemSetParamd(mBeautyHandles, "intensity_chin", intensity_chin);
            fuItemSetParamd(mBeautyHandles, "change_frames", change_frames);
            fuItemSetParamd(mBeautyHandles, "eye_bright", eye_bright);
            fuItemSetParamd(mBeautyHandles, "tooth_whiten", tooth_whiten);
            fuItemSetParamd(mBeautyHandles, "is_beauty_on", is_beauty_on);
            mNeedUpdateFUOptions = false;
        }
        
        // 4. make it beautiful
        unsigned char *in_ptr = yuvData(videoFrame);
        int handle[] = { mBeautyHandles };
        int handleSize = sizeof(handle) / sizeof(handle[0]);
        fuRenderItemsEx2(
                         FU_FORMAT_I420_BUFFER, reinterpret_cast<int*>(in_ptr),
                         FU_FORMAT_I420_BUFFER, reinterpret_cast<int*>(in_ptr),
                         videoFrame->width, videoFrame->height,
                         mFrameID, handle, handleSize,
                         NAMA_RENDER_FEATURE_FULL, NULL);
        videoFrameData(videoFrame, in_ptr);
        delete in_ptr;
    } while(false);
    
    return true;
}

bool FaceUnityPlugin::load(const char *path)
{
    if(mLoaded) {
        return false;
    }
    
    std::string sPath(path);
    folderPath = sPath;
    
    mLoaded = true;
    return true;
}

bool FaceUnityPlugin::unLoad()
{
    if(!mLoaded) {
        return false;
    }
    
    delete[] auth_package;
    
    mLoaded = false;
    return true;
}


bool FaceUnityPlugin::enable()
{
    do {
        
    } while (false);
    return true;
}


bool FaceUnityPlugin::disable()
{
    return true;
}


bool FaceUnityPlugin::setParameter(const char *param)
{
    Document d;
    d.Parse(param);
    
    if(d.HasParseError()) {
        return false;
    }
    
    
    if(d.HasMember("plugin.fu.authdata")) {
        Value& authdata = d["plugin.fu.authdata"];
        if(!authdata.IsArray()) {
            return false;
        }
        auth_package_size = authdata.Capacity();
        auth_package = new char[auth_package_size];
        authdata.GetArray();
        for (int i = 0; i < auth_package_size; i++) {
            auth_package[i] = authdata[i].GetInt();
        }
    }
    
    if(d.HasMember("plugin.fu.param.filter_name")) {
        Value& value = d["plugin.fu.param.filter_name"];
        if(!value.IsString()) {
            return false;
        }
        std::string sName(value.GetString());
        filter_name = sName;
    }
    
    READ_DOUBLE_VALUE_PARAM(d, "plugin.fu.param.filter_level", filter_level)
    READ_DOUBLE_VALUE_PARAM(d, "plugin.fu.param.color_level", color_level)
    READ_DOUBLE_VALUE_PARAM(d, "plugin.fu.param.red_level", red_level)
    READ_DOUBLE_VALUE_PARAM(d, "plugin.fu.param.blur_level", blur_level)
    READ_DOUBLE_VALUE_PARAM(d, "plugin.fu.param.skin_detect", skin_detect)
    READ_DOUBLE_VALUE_PARAM(d, "plugin.fu.param.nonshin_blur_scale", nonshin_blur_scale)
    READ_DOUBLE_VALUE_PARAM(d, "plugin.fu.param.heavy_blur", heavy_blur)
    READ_DOUBLE_VALUE_PARAM(d, "plugin.fu.param.face_shape", face_shape)
    READ_DOUBLE_VALUE_PARAM(d, "plugin.fu.param.face_shape_level", face_shape_level)
    READ_DOUBLE_VALUE_PARAM(d, "plugin.fu.param.eye_enlarging", eye_enlarging)
    READ_DOUBLE_VALUE_PARAM(d, "plugin.fu.param.cheek_thinning", cheek_thinning)
    READ_DOUBLE_VALUE_PARAM(d, "plugin.fu.param.cheek_v", cheek_v)
    READ_DOUBLE_VALUE_PARAM(d, "plugin.fu.param.cheek_narrow", cheek_narrow)
    READ_DOUBLE_VALUE_PARAM(d, "plugin.fu.param.cheek_small", cheek_small)
    READ_DOUBLE_VALUE_PARAM(d, "plugin.fu.param.cheek_oval", cheek_oval)
    READ_DOUBLE_VALUE_PARAM(d, "plugin.fu.param.intensity_nose", intensity_nose)
    READ_DOUBLE_VALUE_PARAM(d, "plugin.fu.param.intensity_forehead", intensity_forehead)
    READ_DOUBLE_VALUE_PARAM(d, "plugin.fu.param.intensity_mouth", intensity_mouth)
    READ_DOUBLE_VALUE_PARAM(d, "plugin.fu.param.intensity_forehead", intensity_forehead)
    READ_DOUBLE_VALUE_PARAM(d, "plugin.fu.param.intensity_chin", intensity_chin)
    READ_DOUBLE_VALUE_PARAM(d, "plugin.fu.param.change_frames", change_frames)
    READ_DOUBLE_VALUE_PARAM(d, "plugin.fu.param.eye_bright", eye_bright)
    READ_DOUBLE_VALUE_PARAM(d, "plugin.fu.param.tooth_whiten", tooth_whiten)
    READ_DOUBLE_VALUE_PARAM(d, "plugin.fu.param.is_beauty_on", is_beauty_on)
    
    return false;
}


//bool FaceUnityPlugin::setBoolParameter(const char *param, bool value)
//{
//    return false;
//}
//
//bool FaceUnityPlugin::setStringParameter(const char *param, const char *value)
//{
//    std::string strParam = param;
//    if (strParam.compare("plugin.fu.authdata") == 0) {
//        auth_package = new char[auth_package_size];
//        memcpy(auth_package, value, auth_package_size);
//        return true;
//    } else if (strParam.compare("plugin.fu.param.filter_name") == 0) {
//        filter_name = value;
//        return true;
//    }
//    return false;
//}
//
//bool FaceUnityPlugin::setIntParameter(const char *param, int32_t value)
//{
//    std::string strParam = param;
//    if (strParam.compare("plugin.fu.authdata.size") == 0) {
//        auth_package_size = value;
//        return true;
//    }
//    return false;
//}
//
//bool FaceUnityPlugin::setDoubleParameter(const char *param, double value)
//{
//    std::string strParam = param;
//    if (strParam.compare("plugin.fu.param.filter_level") == 0) {
//        filter_level = value;
//        return true;
//    } else if(strParam.compare("plugin.fu.param.color_level") == 0) {
//        color_level = value;
//        return true;
//    } else if(strParam.compare("plugin.fu.param.red_level") == 0) {
//        red_level = value;
//        return true;
//    } else if(strParam.compare("plugin.fu.param.blur_level") == 0) {
//        blur_level = value;
//        return true;
//    } else if(strParam.compare("plugin.fu.param.skin_detect") == 0) {
//        skin_detect = value;
//        return true;
//    } else if(strParam.compare("plugin.fu.param.nonshin_blur_scale") == 0) {
//        nonshin_blur_scale = value;
//        return true;
//    } else if(strParam.compare("plugin.fu.param.heavy_blur") == 0) {
//        heavy_blur = value;
//        return true;
//    } else if(strParam.compare("plugin.fu.param.face_shape") == 0) {
//        face_shape = value;
//        return true;
//    } else if(strParam.compare("plugin.fu.param.face_shape_level") == 0) {
//        face_shape_level = value;
//        return true;
//    } else if(strParam.compare("plugin.fu.param.eye_enlarging") == 0) {
//        eye_enlarging = value;
//        return true;
//    } else if(strParam.compare("plugin.fu.param.cheek_thinning") == 0) {
//        cheek_thinning = value;
//        return true;
//    } else if(strParam.compare("plugin.fu.param.cheek_v") == 0) {
//        cheek_v = value;
//        return true;
//    } else if(strParam.compare("plugin.fu.param.cheek_narrow") == 0) {
//        cheek_narrow = value;
//        return true;
//    } else if(strParam.compare("plugin.fu.param.cheek_small") == 0) {
//        cheek_small = value;
//        return true;
//    } else if(strParam.compare("plugin.fu.param.cheek_oval") == 0) {
//        cheek_oval = value;
//        return true;
//    } else if(strParam.compare("plugin.fu.param.intensity_nose") == 0) {
//        intensity_nose = value;
//        return true;
//    } else if(strParam.compare("plugin.fu.param.intensity_forehead") == 0) {
//        intensity_forehead = value;
//        return true;
//    } else if(strParam.compare("plugin.fu.param.intensity_mouth") == 0) {
//        intensity_mouth = value;
//        return true;
//    } else if(strParam.compare("plugin.fu.param.intensity_chin") == 0) {
//        intensity_chin = value;
//        return true;
//    } else if(strParam.compare("plugin.fu.param.change_frames") == 0) {
//        change_frames = value;
//        return true;
//    } else if(strParam.compare("plugin.fu.param.eye_bright") == 0) {
//        eye_bright = value;
//        return true;
//    } else if(strParam.compare("plugin.fu.param.tooth_whiten") == 0) {
//        tooth_whiten = value;
//        return true;
//    } else if(strParam.compare("plugin.fu.param.is_beauty_on") == 0) {
//        is_beauty_on = value;
//        return true;
//    }
//    return false;
//}

void FaceUnityPlugin::release()
{
    fuOnDeviceLost();
    fuDestroyAllItems();
    mNamaInited = false;
    mNeedUpdateFUOptions = true;
    delete[] auth_package;
    folderPath = "";
}

IVideoFramePlugin* createVideoFramePlugin()
{
    return new FaceUnityPlugin();
}
