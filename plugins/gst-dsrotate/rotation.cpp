#include "rotation.h"

VideoRotator::VideoRotator(float width, float height, int divide_count)
    : width_(width), height_(height), divide_count_(divide_count)
{
    rotation_count_ = 0;

    int center_x = (int)((float)(width_)  / 2.0);
    int center_y = (int)((float)(height_) / 2.0);

    // 分割数分、切り取りサイズ、貼り付け位置を持つ Matrix を生成 
    // 大きい方から順に生成
    for (int i = divide_count_ - 1; i >= 1; i--) {

        // (N-1)/N, (N-2)/N, ..., 2/N, 1/N
        int w = (int)(width_  * i / (float)(divide_count_));
        int h = (int)(height_ * i / (float)(divide_count_));

        crop_rects_.emplace_back(cv::Rect(
            cv::Point(center_x - (int)((float)(w) / 2.0), center_y - (int)((float)(h) / 2.0)), 
            cv::Size(w, h))
        );
        crop_mats_.emplace_back(cv::Mat::zeros(w, h, CV_8UC3));

        // 回転方向 (隣り合う矩形同士で逆回転にする)
        int unit = 1;
        if (i % 2) unit = -1;

        std::vector<cv::Mat> affine_mats_part;
        for (int j = 0; j < 360; j++) {
            cv::Mat mat = cv::getRotationMatrix2D(cv::Point2f(w / 2.0, h / 2.0), j * unit, 1.0);
            mat.at<double>(0, 2) += (double)((width_  - w) / 2.0);
            mat.at<double>(1, 2) += (double)((height_ - h) / 2.0);
            affine_mats_part.emplace_back(mat);
        }
        affine_mats_.emplace_back(affine_mats_part);
    }
}

void VideoRotator::rotate(cv::Mat& image)
{
    // 外側から Crop 生成
    for (int i = 0; i <= divide_count_ - 2; i++) {
        image(crop_rects_[i]).copyTo(crop_mats_[i]);
    }

    // 外側から貼り付け
    // NOTE: 「外側に行くにつれて速度を上げる」とかやっても面白いかもしれない
    for (int i = 0; i <= divide_count_ - 2; i++) {
        cv::warpAffine(crop_mats_[i], image, affine_mats_[i][rotation_count_], image.size(), cv::INTER_CUBIC, cv::BORDER_TRANSPARENT);
    }    
    if (++rotation_count_ >= 360) {
        rotation_count_ = 0;
    }        
} 

