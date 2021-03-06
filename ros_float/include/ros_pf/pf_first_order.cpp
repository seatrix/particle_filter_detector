#include "ros/ros.h"
#include "pf_first_order.h"
#include <bfl/wrappers/rng/rng.h> // Wrapper around several rng libraries

#define SYSMODEL_NUMCONDARGUMENTS_MOBILE 2
#define SYSMODEL_DIMENSION_MOBILE        3

#define MU_SYSTEM_NOISE_X 0.0
#define MU_SYSTEM_NOISE_Y 0.0
#define MU_SYSTEM_NOISE_Z 0.0

#define SIGMA_SYSTEM_NOISE_X 0.01
#define SIGMA_SYSTEM_NOISE_Y 0.01
#define SIGMA_SYSTEM_NOISE_Z 1.0

#define MU_USBL_NOISE_X 0.0
#define MU_USBL_NOISE_Y 0.0
#define MU_USBL_NOISE_Z 0.0
#define SIGMA_USBL_NOISE_X 1.0
#define SIGMA_USBL_NOISE_Y 1.0
#define SIGMA_USBL_NOISE_Z 400.0

#define MU_CAMERA_NOISE_X 0.0
#define MU_CAMERA_NOISE_Y 0.0
#define MU_CAMERA_NOISE_Z 0.0
#define SIGMA_CAMERA_NOISE_X 1.0
#define SIGMA_CAMERA_NOISE_Y 1.0
#define SIGMA_CAMERA_NOISE_Z 1.0

#define MU_KELLER_NOISE 0.0
#define SIGMA_KELLER_NOISE 1.0

#define MU_ALTITUDE_FROM_BOTTOM_NOISE 0.0
#define SIGMA_ALTITUDE_FROM_BOTTOM_NOISE 1.0

#define STATE_SIZE 3
#define MEAS_USBL_SIZE 3
#define MEAS_KELLER_SIZE 1
#define MEAS_CAMERA_SIZE 3
#define MEAS_ALTITUDE_SIZE 1


#define PRIOR_MU_X 0.0
#define PRIOR_MU_Y 0.0
#define PRIOR_MU_Z 50.0
#define PRIOR_COV_X 100.0
#define PRIOR_COV_Y 100.0
#define PRIOR_COV_Z 50.0

#define NUM_SAMPLES  2000

namespace BFL
{

grid_map::GridMap* mapPtr;

//bool ROVModel::SampleFrom (Sample<MatrixWrapper::ColumnVector>& one_sample, int method, void * args)
bool ROVModel::SampleFrom (Sample<ColumnVector>& one_sample, int method, void* args) const
{
    // retreive current state and control input
    ColumnVector state = ConditionalArgumentGet(0);
    ColumnVector velocity  = ConditionalArgumentGet(1);

    Sample<ColumnVector> noise;
    _additiveNoise.SampleFrom(noise, method, args);
    one_sample.ValueSet(state + velocity + noise.ValueGet());
    return true;
}


Probability USBLModelPdf::ProbabilityGet(const MatrixWrapper::ColumnVector& measurement) const
{
    ColumnVector state = ConditionalArgumentGet(0);
    ColumnVector vel  = ConditionalArgumentGet(1);

    double roll = measurement(4);
    double pitch = measurement(5);
    double yaw = measurement(6);

    ColumnVector expected_measurement(3);
    expected_measurement(1) = state(1);
    expected_measurement(2) = state(2);
    expected_measurement(3) = state(3);

    ColumnVector actualmeasurement(3);
    actualmeasurement(1) = measurement(1);
    actualmeasurement(2) = measurement(2);
    actualmeasurement(3) = measurement(3);

    (void) roll;
    (void) pitch;
    (void) yaw;
    // error
    return _measNoise.ProbabilityGet(expected_measurement-actualmeasurement)+0.00001/*It's a kind of magic*/;
}

Probability KellerModelPdf::ProbabilityGet(const MatrixWrapper::ColumnVector& measurement) const
{
    ColumnVector state = ConditionalArgumentGet(0);
    ColumnVector vel  = ConditionalArgumentGet(1);
    ColumnVector expected_measurement(1);
    expected_measurement(1) = state(3);

    return _measNoise.ProbabilityGet(expected_measurement-measurement);
}

Probability AltitudeModelPdf::ProbabilityGet(const MatrixWrapper::ColumnVector &measurement) const
{
    ColumnVector state = ConditionalArgumentGet(0);
    ColumnVector alt = ConditionalArgumentGet(1);
    ColumnVector expected_measurement(1);
    grid_map::Position position(state(1), state(2));
    float mappoint = mapPtr->atPosition("elevation",position);
    expected_measurement(1) =  - mappoint - state(3);

    std::cout << "mappoint in " << state(1)<< " "<< state(2)<< " " << mappoint  << "   z " << state(3) << "   expected_measurement " << expected_measurement(1) << "   measurement " << measurement(1) << std::endl;

    return _measNoise.ProbabilityGet(expected_measurement - measurement);
}

MyParticleFilter::MyParticleFilter(ros::NodeHandle &n):_n(n)
{

    GridMapRosConverter::loadFromBag("/home/emanuele/Desktop/grid_map_big_island_5ms.bag","grid_map", map);
    mapPtr = &map;

    // system noise
    ColumnVector sysNoise_Mu(STATE_SIZE);
    sysNoise_Mu(1) = MU_SYSTEM_NOISE_X;
    sysNoise_Mu(2) = MU_SYSTEM_NOISE_Y;
    sysNoise_Mu(3) = MU_SYSTEM_NOISE_Z;

    SymmetricMatrix sysNoise_Cov(STATE_SIZE);
    sysNoise_Cov(1,1) = SIGMA_SYSTEM_NOISE_X;
    sysNoise_Cov(1,2) = 0.0;
    sysNoise_Cov(1,3) = 0.0;
    sysNoise_Cov(2,1) = 0.0;
    sysNoise_Cov(2,2) = SIGMA_SYSTEM_NOISE_Y;
    sysNoise_Cov(2,3) = 0.0;
    sysNoise_Cov(3,1) = 0.0;
    sysNoise_Cov(3,2) = 0.0;
    sysNoise_Cov(3,3) = SIGMA_SYSTEM_NOISE_Z;

    Gaussian system_Uncertainty(sysNoise_Mu, sysNoise_Cov);
    sys_pdf = new ROVModel(system_Uncertainty);
    sys_model = new SystemModel<ColumnVector>(sys_pdf);


    // measurement model of the usbl
    ColumnVector usbl_noise_Mu(MEAS_USBL_SIZE);
    usbl_noise_Mu(1) = MU_USBL_NOISE_X;
    usbl_noise_Mu(2) = MU_USBL_NOISE_Y;
    usbl_noise_Mu(3) = MU_USBL_NOISE_Z;
    SymmetricMatrix usbl_noise_Cov(MEAS_USBL_SIZE);
    usbl_noise_Cov(1,1) = SIGMA_USBL_NOISE_X;
    usbl_noise_Cov(1,2) = 0.0;
    usbl_noise_Cov(1,3) = 0.0;
    usbl_noise_Cov(2,1) = 0.0;
    usbl_noise_Cov(2,2) = SIGMA_USBL_NOISE_Y;
    usbl_noise_Cov(2,3) = 0.0;
    usbl_noise_Cov(3,1) = 0.0;
    usbl_noise_Cov(3,2) = 0.0;
    usbl_noise_Cov(3,3) = SIGMA_USBL_NOISE_Z;

    Gaussian measurement_Uncertainty(usbl_noise_Mu, usbl_noise_Cov);
    usbl_meas_pdf = new USBLModelPdf(measurement_Uncertainty);
    usbl_meas_model = new MeasurementModel<ColumnVector,ColumnVector>(usbl_meas_pdf);


    // measurement model of the keller
    ColumnVector keller_noise_Mu(MEAS_KELLER_SIZE);
    keller_noise_Mu(1) = MU_KELLER_NOISE;
    SymmetricMatrix keller_noise_Cov(MEAS_KELLER_SIZE);
    keller_noise_Cov(1,1) = SIGMA_KELLER_NOISE;

    Gaussian keller_measurement_Uncertainty(keller_noise_Mu, keller_noise_Cov);
    keller_meas_pdf = new KellerModelPdf(keller_measurement_Uncertainty);
    keller_meas_model = new MeasurementModel<ColumnVector,ColumnVector>(keller_meas_pdf);

    // measurement model of the altitide from bottom
    ColumnVector alt_noise_Mu(MEAS_ALTITUDE_SIZE);
    alt_noise_Mu(1) = MU_ALTITUDE_FROM_BOTTOM_NOISE;
    SymmetricMatrix alt_noise_Cov(MEAS_ALTITUDE_SIZE);
    alt_noise_Cov(1,1) = SIGMA_ALTITUDE_FROM_BOTTOM_NOISE;

    Gaussian altitude_measurement_Uncertainty(alt_noise_Mu, alt_noise_Cov);
    alt_meas_pdf = new AltitudeModelPdf(altitude_measurement_Uncertainty);
    altitude_meas_model = new MeasurementModel<ColumnVector,ColumnVector>(alt_meas_pdf);


    // prior distribution
    ColumnVector prior_Mu(STATE_SIZE);
    prior_Mu(1) = PRIOR_MU_X;
    prior_Mu(2) = PRIOR_MU_Y;
    prior_Mu(3) = PRIOR_MU_Z;

    SymmetricMatrix prior_Cov(STATE_SIZE);
    prior_Cov(1,1) = PRIOR_COV_X;
    prior_Cov(1,2) = 0.0;
    prior_Cov(1,3) = 0.0;
    prior_Cov(2,1) = 0.0;
    prior_Cov(2,2) = PRIOR_COV_Y;
    prior_Cov(2,3) = 0.0;
    prior_Cov(3,1) = 0.0;
    prior_Cov(3,2) = 0.0;
    prior_Cov(3,3) = PRIOR_COV_Z;

    Gaussian prior(prior_Mu, prior_Cov);
    vector<Sample<ColumnVector> > prior_samples(NUM_SAMPLES);
    prior.SampleFrom(prior_samples, NUM_SAMPLES,CHOLESKY, NULL);
    MCPdf<ColumnVector> prior_discr(NUM_SAMPLES,3);
    prior_discr.ListOfSamplesSet(prior_samples);

    filter = new BootstrapFilter<ColumnVector,ColumnVector>(&prior_discr, 0, NUM_SAMPLES/4.0);

    // init ROS stuff
    imu_sub = n.subscribe("/imu/data_w_orientation", 1000, &MyParticleFilter::imuCb, this);
    usbl_sub = n.subscribe("/usbl/pose_projected", 1000, &MyParticleFilter::usblCb, this);
    kell_sub= n.subscribe("/filter/fluid_pressure/depth/", 1000, &MyParticleFilter::kellCb, this);
    odometry_sub = n.subscribe("/cam/odometry", 1000, &MyParticleFilter::camCb, this);
    alt_pub = n.subscribe("/filter/alt/data", 1000, &MyParticleFilter::altCb, this);

    particle_pub = _n.advertise<geometry_msgs::PoseArray>("/particle_cloud", 1);
    pose_pub = n.advertise<geometry_msgs::PoseStamped>("/estimate",1);


    altErrorPublisher = n.advertise<ros_float::altitude>("/error_measurement",1);


    particleTimer = n.createTimer(ros::Duration(1), &MyParticleFilter::timerCallback, this);
    }

void MyParticleFilter::publishPose() // publish of the estimate of the state
{
    Pdf<ColumnVector> * posterior = filter->PostGet();
    ColumnVector pose = posterior->ExpectedValueGet();
    SymmetricMatrix pose_cov = posterior->CovarianceGet();

    geometry_msgs::PoseStamped pose_msg;
    pose_msg.header.frame_id = "map";
    pose_msg.pose.position.x = -pose(1);
    pose_msg.pose.position.y = -pose(2);
    pose_msg.pose.position.z = -pose(3);

    // Query of the estimate (la freccia grande) and publish the estimate
    grid_map::Position position(pose(1), pose(2));
    float mappoint = mapPtr->atPosition("elevation",position);

    double measurement = lastAltitudeReceived.altitude;
    double expected_measurement =  - mappoint - pose(3);
    std::cout << "Estimate Query mappoint in " << pose(1)<< " "<< pose(2)<< " " << mappoint  << " z " << "   expected_measurement "
              << expected_measurement << "   measurement " << measurement << std::endl;

    ros_float::altitude alterrmsg;
    alterrmsg.header = lastAltitudeReceived.header;
    alterrmsg.altitude = expected_measurement - measurement;
    altErrorPublisher.publish(alterrmsg);


    static tf2_ros::TransformBroadcaster br;
    geometry_msgs::TransformStamped transformStamped;

    transformStamped.header.stamp = ros::Time::now();
    transformStamped.header.frame_id = "map";
    transformStamped.child_frame_id = "base_link";

    transformStamped.transform.translation.x = -pose(1);
    transformStamped.transform.translation.y = -pose(2);
    transformStamped.transform.translation.z = -pose(3);
    tf2::Quaternion q;
    q.setRPY(0, 0, 0);
    transformStamped.transform.rotation.x = q.x();
    transformStamped.transform.rotation.y = q.y();
    transformStamped.transform.rotation.z = q.z();
    transformStamped.transform.rotation.w = q.w();

    br.sendTransform(transformStamped);
    pose_pub.publish(pose_msg);
}
};


