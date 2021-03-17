#ifndef __ROTATION_H__
#define __ROTATION_H__

#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/highgui/highgui.hpp"

#include <iostream>
#include <vector>


/*********************************/
/*** VideoRoatator             ***/
/*********************************/
// width x height サイズの映像を 1/N ずつ中心固定で割って、回転させる。
// ( N = divide_count )
class VideoRotator
{
public:
    VideoRotator(float width, float height, int divide_count);

public:
    void rotate(cv::Mat& image);

private:
    int rotation_count_ = 0;

    float width_;
    float height_;
    int divide_count_;

    std::vector<cv::Rect> crop_rects_;
    std::vector<cv::Mat> crop_mats_;
    std::vector<std::vector<cv::Mat>> affine_mats_;
};


#endif /* __ROTATION_H__ */
