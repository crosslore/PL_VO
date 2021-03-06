//
// Created by rain on 17-12-29.
//

#include "Frame.h"

namespace PL_VO
{

size_t Frame::gCount = 0;

Frame::Frame(const double &timeStamp, Camera *pCamera, LineFeature *pLineFeature, PointFeature *pPointFeature)
             : mpCamera(pCamera), mpLineFeature(pLineFeature), mpPointFeature(pPointFeature)
{
    mID = gCount;
    gCount++;
    mtimeStamp = timeStamp;
    mImageHeight = pCamera->mImageHeight;
    mImageWidth = pCamera->mImageWidth;

    Tcw.so3().setQuaternion(Eigen::Quaterniond::Identity());
    Tcw.translation() = Eigen::Vector3d(0, 0, 0);
    Twc = Tcw.inverse();

    mvPointLevelSigma2 = pPointFeature->mvLevelSigma2;
    mvPointInvLevelSigma2 = pPointFeature->mvInvLevelSigma2;
    mvPointScaleFactors = pPointFeature->mvScaleFactor;
    mvPointInvScaleFactors = pPointFeature->mvInvScaleFactor;

    mvLineLevelSigma2 = pLineFeature->mvLevelSigma2;
    mvLineInvLevelSigma2 = pLineFeature->mvInvLevelSigma2;
    mvLineScaleFactors = pLineFeature->mvScaleFactor;
    mvLineInvScaleFactors = pLineFeature->mvInvScaleFactor;
}

Frame::Frame(const Frame &frame)
{
    Tcw = frame.Tcw;
    Twc = frame.Twc;

    mpCamera = frame.mpCamera;
    mpLineFeature = frame.mpLineFeature;
    mpPointFeature = frame.mpPointFeature;
    mImageWidth = frame.mImageWidth;
    mImageHeight = frame.mImageHeight;

    mID = frame.GetFrameID();
    mtimeStamp = frame.mtimeStamp;
    mpointDesc = frame.mpointDesc;
    mlineDesc = frame.mlineDesc;
    mvKeyPoint.assign(frame.mvKeyPoint.begin(), frame.mvKeyPoint.end());
    mvKeyLine.assign(frame.mvKeyLine.begin(), frame.mvKeyLine.end());
    mvKeyPointUn.assign(frame.mvKeyPointUn.begin(), frame.mvKeyPointUn.end());
    mvKeyLineUn.assign(frame.mvKeyLineUn.begin(), frame.mvKeyLineUn.end());

    mvPointLevelSigma2.assign(frame.mvPointLevelSigma2.begin(), frame.mvPointLevelSigma2.end());
    mvPointInvLevelSigma2.assign(frame.mvPointInvLevelSigma2.begin(), frame.mvPointInvLevelSigma2.end());
    mvPointScaleFactors.assign(frame.mvPointScaleFactors.begin(), frame.mvPointScaleFactors.end());
    mvPointInvScaleFactors.assign(frame.mvPointInvScaleFactors.begin(), frame.mvPointInvScaleFactors.end());

    mvLineLevelSigma2.assign(frame.mvLineLevelSigma2.begin(), frame.mvLineLevelSigma2.end());
    mvLineInvLevelSigma2.assign(frame.mvLineInvLevelSigma2.begin(), frame.mvLineInvLevelSigma2.end());
    mvLineScaleFactors.assign(frame.mvLineScaleFactors.begin(), frame.mvLineScaleFactors.end());
    mvLineInvScaleFactors.assign(frame.mvLineInvScaleFactors.begin(), frame.mvLineInvScaleFactors.end());

    mvpPointFeature2D.assign(frame.mvpPointFeature2D.begin(), frame.mvpPointFeature2D.end());
    mvpLineFeature2D.assign(frame.mvpLineFeature2D.begin(), frame.mvpLineFeature2D.end());
    mvpMapPoint.assign(frame.mvpMapPoint.begin(), frame.mvpMapPoint.end());
    mvpMapLine.assign(frame.mvpMapLine.begin(), frame.mvpMapLine.end());
}

Frame::~Frame()
{
    for (auto it = mvpPointFeature2D.begin(); it != mvpPointFeature2D.end(); it ++)
        if (NULL != *it)
        {
            delete *it;
            *it = NULL;
        }
    mvpPointFeature2D.clear();

    for (auto it = mvpLineFeature2D.begin(); it != mvpLineFeature2D.end(); it ++)
        if (NULL != *it)
        {
            delete *it;
            *it = NULL;
        }
    mvpLineFeature2D.clear();

    for (auto it = mvpMapPoint.begin(); it != mvpMapPoint.end(); it ++)
        if (NULL != *it)
        {
            delete *it;
            *it = NULL;
        }
    mvpMapPoint.clear();

    for (auto it = mvpMapLine.begin(); it != mvpMapLine.end(); it ++)
        if (NULL != *it)
        {
            delete *it;
            *it = NULL;
        }
    mvpMapLine.clear();
}

size_t Frame::GetFrameID()
{
    unique_lock<mutex> lock(mMutexPose);
    return mID;
}

const size_t Frame::GetFrameID() const
{
    return mID;
}

Eigen::Vector3d Frame::GetCameraCenter()
{
    return Tcw.inverse().translation();
}

void Frame::detectFeature(const cv::Mat &imagergb, const cv::Mat &imagedepth)
{
    double minLinelength = PL_VO::Config::minLineLength() * min(mpCamera->mImageHeight, mpCamera->mImageWidth);

//    TicToc tictocdetectfeature;

    if (Config::plInParallel())
    {
        auto linedetect = async(launch::async, &LineFeature::detectLinefeature, mpLineFeature, imagergb,
                                ref(mvKeyLine), ref(mlineDesc), minLinelength);
        auto pointdetect = async(launch::async, &PointFeature::detectPointfeature, mpPointFeature, imagergb,
                                 ref(mvKeyPoint), ref(mpointDesc));

        linedetect.wait();
        pointdetect.wait();
    }
    else
    {
        if (Config::hasPoints())
            mpPointFeature->detectPointfeature(imagergb, mvKeyPoint, mpointDesc);

        if (Config::hasLines())
            mpLineFeature->detectLinefeature(imagergb, mvKeyLine, mlineDesc, minLinelength);
    }

//    cout << "the paraller detect the point feature and line feature (ms): " << tictocdetectfeature.toc() << endl;

}

void Frame::UndistortPointFeature()
{
    if (mpCamera->GetDistortionPara()[0] == 0.)
    {
        mvKeyPointUn.assign(mvKeyPoint.begin(), mvKeyPoint.end());
        return;
    }

    cv::Mat mat((int)mvKeyPoint.size(), 2, CV_32F);
    for (int i = 0; i < mvKeyPoint.size(); i++)
    {
        mat.at<float>(i, 0) = mvKeyPoint[i].pt.x;
        mat.at<float>(i, 1) = mvKeyPoint[i].pt.y;
    }

    mat = mat.reshape(2);

    cv::Mat DistCoef(5,1,CV_32F);
    DistCoef.at<float>(0) = (float)mpCamera->GetDistortionPara2()[0];
    DistCoef.at<float>(1) = (float)mpCamera->GetDistortionPara2()[1];
    DistCoef.at<float>(2) = (float)mpCamera->GetDistortionPara2()[2];
    DistCoef.at<float>(3) = (float)mpCamera->GetDistortionPara2()[3];
    DistCoef.at<float>(4) = (float)mpCamera->GetDistortionPara2()[4];

    cv::Mat K;
    K = Converter::toCvMat(mpCamera->GetCameraIntrinsic());

    cv::undistortPoints(mat, mat, K,  DistCoef, cv::Mat(), K);

    mat = mat.reshape(1);

    mvKeyPointUn.resize(mvKeyPoint.size());

    for (int i = 0; i < mvKeyPoint.size(); i++)
    {
        cv::KeyPoint kp = mvKeyPoint[i];
        kp.pt.x = mat.at<float>(i, 0);
        kp.pt.y = mat.at<float>(i, 1);
        mvKeyPointUn[i] = kp;
    }
}

void Frame::UndistortLineFeature()
{
    // Line feature
    if (mpCamera->GetDistortionPara()[0] == 0)
    {
        mvKeyLineUn.assign(mvKeyLine.begin(), mvKeyLine.end());
        return ;
    }

    cv::Mat mat2(2*(int)mvKeyLine.size(), 2, CV_32F);
    for (int i = 0; i < mvKeyLine.size(); i++)
    {
        mat2.at<float>(2*i, 0) = mvKeyLine[i].startPointX;
        mat2.at<float>(2*i, 1) = mvKeyLine[i].startPointY;
        mat2.at<float>(2*i+1, 0) = mvKeyLine[i].endPointX;
        mat2.at<float>(2*i+1, 1) = mvKeyLine[i].endPointY;
    }

    mat2 = mat2.reshape(2);

    cv::Mat DistCoef2(5,1,CV_32F);
    DistCoef2.at<float>(0) = (float)mpCamera->GetDistortionPara2()[0];
    DistCoef2.at<float>(1) = (float)mpCamera->GetDistortionPara2()[1];
    DistCoef2.at<float>(2) = (float)mpCamera->GetDistortionPara2()[2];
    DistCoef2.at<float>(3) = (float)mpCamera->GetDistortionPara2()[3];
    DistCoef2.at<float>(4) = (float)mpCamera->GetDistortionPara2()[4];

    cv::Mat K2;
    K2 = Converter::toCvMat(mpCamera->GetCameraIntrinsic());

    cv::undistortPoints(mat2, mat2, K2,  DistCoef2, cv::Mat(), K2);

    mat2 = mat2.reshape(1);

    mvKeyLineUn.resize(mvKeyLine.size());

    for (int i = 0; i < mvKeyLine.size(); i++)
    {
        cv::line_descriptor::KeyLine kl = mvKeyLine[i];

        if (min(mat2.at<float>(2*i, 0), mat2.at<float>(2*i+1, 0)) <= 0)
        {
            LOG(ERROR) << min(mat2.at<float>(2*i, 0), mat2.at<float>(2*i+1, 0)) << endl;
            continue;
        }

        if (min(mat2.at<float>(2*i, 1), mat2.at<float>(2*i+1, 1)) <= 0)
        {
            LOG(ERROR) << min(mat2.at<float>(2*i, 1), mat2.at<float>(2*i+1, 1)) << endl;
            continue;
        }

        if (max(mat2.at<float>(2*i, 0), mat2.at<float>(2*i+1, 0)) >= mImageWidth)
        {
            LOG(ERROR) << max(mat2.at<float>(2*i, 0), mat2.at<float>(2*i+1, 0) ) << endl;
            continue;
        }

        if (max(mat2.at<float>(2*i, 1), mat2.at<float>(2*i+1, 1)) >= mImageHeight)
        {
            LOG(ERROR) << max(mat2.at<float>(2*i, 1), mat2.at<float>(2*i+1, 1) ) << endl;
            continue;
        }

        kl.startPointX = mat2.at<float>(2*i, 0);
        kl.startPointY = mat2.at<float>(2*i, 1);
        kl.endPointX = mat2.at<float>(2*i+1, 0);
        kl.endPointY = mat2.at<float>(2*i+1, 1);
        mvKeyLineUn[i] = kl;
    }
}

void Frame::UndistortKeyFeature()
{
    if (Config::plInParallel())
    {
        auto undistortPoint = async(launch::async, &Frame::UndistortPointFeature, this);
        auto undistorLine = async(launch::async, &Frame::UndistortLineFeature, this);

        undistortPoint.wait();
        undistorLine.wait();
    }
    else
    {
        UndistortPointFeature();
        UndistortLineFeature();
    }

}

void Frame::ComputeImageBounds(const cv::Mat &image)
{
    cv::Mat K;
    double minX, maxX, minY, maxY;
    cv::Mat mat(4, 2, CV_32F);
    cv::Mat DistCoef(5,1,CV_32F);

    K = Converter::toCvMat(mpCamera->GetCameraIntrinsic());

    DistCoef.at<float>(0) = (float)mpCamera->GetDistortionPara2()[0];
    DistCoef.at<float>(1) = (float)mpCamera->GetDistortionPara2()[1];
    DistCoef.at<float>(2) = (float)mpCamera->GetDistortionPara2()[2];
    DistCoef.at<float>(3) = (float)mpCamera->GetDistortionPara2()[3];
    DistCoef.at<float>(4) = (float)mpCamera->GetDistortionPara2()[4];

    mat.at<float>(0, 0) = 0.0;
    mat.at<float>(0, 1) = 0.0;
    mat.at<float>(1, 0) = mImageWidth;
    mat.at<float>(1, 1) = 0.0;
    mat.at<float>(2, 0) = 0.0;
    mat.at<float>(2, 1) = mImageHeight;
    mat.at<float>(3, 0) = mImageWidth;
    mat.at<float>(3, 1) = mImageHeight;

    mat=mat.reshape(2);
    cv::undistortPoints(mat, mat, K, DistCoef, cv::Mat(), K);

    mat=mat.reshape(1);

    minX = min(mat.at<float>(0,0),mat.at<float>(2,0));
    maxX = max(mat.at<float>(1,0),mat.at<float>(3,0));
    minY = min(mat.at<float>(0,1),mat.at<float>(1,1));
    maxY = max(mat.at<float>(2,1),mat.at<float>(3,1));

    CHECK(min(minX, minY) > 0) << "the min x of the undistorted image is wrong " << endl;
    CHECK(maxX < mImageWidth) << "the max x of the undistorted image is wrong " << endl;
    CHECK(maxY < mImageHeight) << "the max y of the undistorted image is wrong " << endl;
}

void Frame::matchLPFeature(const cv::Mat &pointdesc1, const cv::Mat &pointdesc2, vector<cv::DMatch> &vpointmatches12,
                           const cv::Mat &linedesc1, const cv::Mat &linedesc2, vector<cv::DMatch> &vlinematches12)
{
    if (Config::plInParallel())
    {
        auto linematch = async(launch::async, &LineFeature::matchLineFeatures, mpLineFeature, ref(linedesc1),
                               ref(linedesc2), ref(vlinematches12));
        auto pointmatch = async(launch::async, &PointFeature::matchPointFeatures, mpPointFeature, ref(pointdesc1),
                                ref(pointdesc2), ref(vpointmatches12));

        linematch.wait();
        pointmatch.wait();
    }
    else
    {
        mpPointFeature->matchPointFeatures(pointdesc1, pointdesc2, vpointmatches12);
        mpLineFeature->matchLineFeatures(linedesc1, linedesc2, vlinematches12);
    }
}

void Frame::refineLPMatches(const vector<cv::KeyPoint> &mvKeyPoint1, const vector<cv::KeyPoint> &mvKeyPoint2,
                            const vector<cv::line_descriptor::KeyLine> &mvKeyLine1,
                            const vector<cv::line_descriptor::KeyLine> &mvKeyLine2,
                            const vector<cv::DMatch> &vpointMatches12, vector<cv::DMatch> &vpointRefineMatches12,
                            const vector<cv::DMatch> &vlineMatches12, vector<cv::DMatch> &vlineRefineMatches12)
{
    if (Config::plInParallel())
    {
        auto linematch = async(launch::async, &LineFeature::refineMatchesWithFundamental, mpLineFeature, mvKeyLine1,
                                mvKeyLine2, vlineMatches12);
        auto pointmatch = async(launch::async, &PointFeature::refineMatchesWithFundamental, mpPointFeature, mvKeyPoint1,
                                mvKeyPoint2, vpointMatches12);
        linematch.wait();
        pointmatch.wait();

        vlineRefineMatches12 = linematch.get();
        vpointRefineMatches12 = pointmatch.get();
    }
    else
    {
        vlineRefineMatches12 = mpLineFeature->refineMatchesWithFundamental(mvKeyLine1, mvKeyLine2, vlineMatches12);
        vpointRefineMatches12 = mpPointFeature->refineMatchesWithFundamental(mvKeyPoint1,mvKeyPoint2, vpointMatches12);
    }
}

double Frame::FindDepth(const cv::Point2f &point, const cv::Mat &imagedepth)
{
    int x = cvRound(point.x);
    int y = cvRound(point.y);

    CHECK(x >= 0 && x <= mImageWidth);
    CHECK(y >= 0 && y <= mImageHeight);

    ushort d = imagedepth.ptr<ushort>(y)[x];

    if (d!=0)
    {
        return double(d)/mpCamera->mdepthscale;
    }
    else
    {
        int dx[4] = {-1,0,1,0};
        int dy[4] = {0,-1,0,1};
        for (int i = 0; i < 4; i++)
        {
            if ((x+dx[i]) < 0 || (x+dx[i]) > mImageWidth)
            {
                continue;
            }
            if ((y+dy[i]) < 0 || (y+dy[i]) > mImageHeight)
            {
                continue;
            }

            d = imagedepth.ptr<ushort>(y+dy[i])[x+dx[i]];
            if (d != 0)
            {
                return double(d)/mpCamera->mdepthscale;
            }
        }
    }

    return -1.;
}

void Frame::UnprojectPointStereo(const cv::Mat &imageDepth)
{
    mvpPointFeature2D.resize(mvKeyPointUn.size());

    cv::KeyPoint kp;
    Eigen::Vector3d Point3dw;
    for (size_t i = 0, iend = mvKeyPointUn.size(); i < iend; i++)
    {
        kp = mvKeyPointUn[i];
        double d = FindDepth(mvKeyPoint[i].pt, imageDepth);

        PointFeature2D *ppointFeature2D = new PointFeature2D(Converter::toVector2d(kp.pt), kp.octave, kp.response);

        if (d > 0)
        {
            Point3dw = mpCamera->Pixwl2World(Converter::toVector2d(kp.pt), Eigen::Quaterniond::Identity(), Eigen::Vector3d(0, 0, 0), d);
        }
        else
        {
            Point3dw.setZero();
            ppointFeature2D->mbinlier = false;
        }

        // the pointfeature2d's ID is setted by the match

        ppointFeature2D->mPoint3dw = Point3dw;

        mvpPointFeature2D[i] = ppointFeature2D;
    }
}

void Frame::UnprojectLineStereo(const cv::Mat &imageDepth)
{
    mvpLineFeature2D.resize(mvKeyLineUn.size());

    cv::line_descriptor::KeyLine klUn;
    cv::line_descriptor::KeyLine kl;

    for (size_t i = 0, iend = mvKeyLineUn.size(); i < iend; i++)
    {
        klUn = mvKeyLineUn[i];
        kl = mvKeyLine[i];

        cv::Point2f startPointUn2f;
        cv::Point2f endPointUn2f;
        cv::Point2f startPoint2f;
        cv::Point2f endPoint2f;

        startPointUn2f = cv::Point2f(klUn.startPointX, klUn.startPointY);
        endPointUn2f = cv::Point2f(klUn.endPointX, klUn.endPointY);

        startPoint2f = cv::Point2f(kl.startPointX, kl.startPointY);
        endPoint2f = cv::Point2f(kl.endPointX, kl.endPointY);

        // !!!notice: use the distored image
        double d1 = FindDepth(startPoint2f, imageDepth);
        double d2 = FindDepth(endPoint2f, imageDepth);
        Eigen::Vector3d startPoint3dw;
        Eigen::Vector3d endPoint3dw;

        LineFeature2D *plineFeature2D = new LineFeature2D(Converter::toVector2d(startPointUn2f), Converter::toVector2d(endPointUn2f),
                                                          kl.octave, kl.response);

        if (d1 > 0)
        {

            startPoint3dw = mpCamera->Pixwl2World(Converter::toVector2d(startPointUn2f), Eigen::Quaterniond::Identity(),
                                                  Eigen::Vector3d(0, 0, 0), d1);
        }
        else
        {
            startPoint3dw.setZero(); // to set the pose zero and use the other observation to calculate the pose
            plineFeature2D->mbinlier = false;
        }

        if (d2 > 0)
        {
            endPoint3dw = mpCamera->Pixwl2World(Converter::toVector2d(endPointUn2f), Eigen::Quaterniond::Identity(),
                                                Eigen::Vector3d(0, 0, 0), d2);
        }
        else
        {
            endPoint3dw.setZero();
            plineFeature2D->mbinlier = false;
        }

        plineFeature2D->mStartPoint3dw = startPoint3dw;
        plineFeature2D->mEndPoint3dw = endPoint3dw;

        mvpLineFeature2D[i]= plineFeature2D;
    }
}

void Frame::UnprojectStereo(const cv::Mat &imageDepth)
{
    if (Config::plInParallel())
    {
        auto pointStereo = async(launch::async, &Frame::UnprojectPointStereo, this, imageDepth);
        auto lineStereo = async(launch::async, &Frame::UnprojectLineStereo, this, imageDepth);

        pointStereo.wait();
        lineStereo.wait();
    }
    else
    {
        UnprojectPointStereo(imageDepth);
        UnprojectLineStereo(imageDepth);
    }
}

void Frame::SetKeyFrameFlag(bool flag)
{
    unique_lock<mutex> lock(mMutexPose);
    mbisKeyFrame = true;
}

bool Frame::isKeyFrame()
{
    unique_lock<mutex> lock(mMutexPose);
    return mbisKeyFrame;
}

Sophus::SE3d Frame::GetPose()
{
    unique_lock<mutex> lock(mMutexPose);
    return Tcw;
}

void Frame::SetPose(Sophus::SE3d Tcw_)
{
    unique_lock<mutex> lock(mMutexPose);
    Tcw = Tcw_;
}

void Frame::MapLinePointShow()
{
    cout << "the frame MapPoint debug show: " << "frame ID: " << GetFrameID() << " frame timestamp: " << to_string(mtimeStamp) << endl;

    bool bMapPointShow = true;
    bool bMapLineShow = true;

    if (bMapPointShow)
    {
        cout << "the MapPoint show " << endl;

        for (auto pMapPoint : mvpMapPoint)
        {
//            cout << "ID: " << pMapPoint->mID << ", ";
//            cout << "3D: " << pMapPoint->mPosew[0] << " " << pMapPoint->mPosew[1] << " " << pMapPoint->mPosew[2] << " | ";
//
//            map<size_t, PointFeature2D*>::iterator it;
//            it = pMapPoint->mmpPointFeature2D.begin();
//
//            while(it != pMapPoint->mmpPointFeature2D.end())
//            {
//                cout << it->second->mpixel[0] << " " << it->second->mpixel[1] << " | ";
//                it++;
//            }
//            cout << endl;

            cout << pMapPoint->GetObservedInliersNum() << " / " << pMapPoint->GetObservedNum() << endl;

        }
    }

    if (bMapLineShow)
    {
        cout << endl << "the MapLine show " << endl;

        for (auto pMapLine : mvpMapLine)
        {
//            cout << "ID: " << pMapLine->mID << " ";
//            cout << "3D: " << pMapLine->mPoseStartw[0] << " " << pMapLine->mPoseStartw[1] << " " << pMapLine->mPoseStartw[2] << ", "
//                 << pMapLine->mPoseEndw[0] << " " << pMapLine->mPoseEndw[1] << " " << pMapLine->mPoseEndw[2] << " | ";
//            map<size_t, LineFeature2D*>::iterator it;
//            it = pMapLine->mmpLineFeature2D.begin();
//
//            while(it != pMapLine->mmpLineFeature2D.end())
//            {
//                cout << it->second->mStartpixel[0] << " " << it->second->mStartpixel[1] << ", "
//                     << it->second->mEndpixel[0] << " " << it->second->mEndpixel[1] << " | ";
//                it++;
//            }
//            cout << endl;
            cout << pMapLine->GetObservedInliersNum() << " / " << pMapLine->GetObservedNum() << endl;
        }
    }
}


} // namespace PL_VO
