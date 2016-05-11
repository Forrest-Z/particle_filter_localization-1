#include "PointRobot.h"
#include "RoboPath.h"

#define LOCALIZETHRESHOLD .05

/**
    Constructor
    @param fname The filename containing all destination points
    @param VARIANCE The amount of error we are willing to allow
*/
PointRobot::PointRobot(char* fname, double SPEED, double VARIANCE) {
    std::string map_name = "src/PointRobot/src/map.info";
    this->MAP_WIDTH = 2000;
    this->MAP_HEIGHT = 700;
    this->MAP_DATA = new int8_t[this->MAP_WIDTH*this->MAP_HEIGHT];
    this->MAP_RESOLUTION = 0.0633;
    this->read_image(map_name.c_str());
    this->destinations = this->read_file(fname);
    this->VARIANCE = VARIANCE;
    this->ANGULAR_VELOCITY = 0.0;
    this->DEFAULT_SPEED = SPEED;
    this->localizer = new Localizer(MAP_DATA, MAP_WIDTH, MAP_HEIGHT, MAP_RESOLUTION);
    this->sonar_change = new bool(false);
}

void PointRobot::whereAmI() {
    suspectedLocation = localizer->update_location(this->pose, this->kinect_data, this->sonar_data, sonar_change);
    geometry_msgs::PoseArray arr = localizer->get_particle_poses();
    point_cloud_pub.publish(arr);

    if(suspectedLocation->weight > LOCALIZETHRESHOLD){
        //path whatever
        printf("nailed it!\n");
        std::vector<MyPoint> pointstest = astar(suspectedLocation->x, suspectedLocation->y, 10, 10);
        // for(int i = 0; i < pointstest.size(); i++)
    }
}

void PointRobot::updateMap() {
    // DO NOTHING FOR NOW
}

/**
    The callback function responsible for handling any information
    retrieved from /r1/odom
    @param msgs The message received from /r1/odom
*/
void PointRobot::odomCallback(nav_msgs::Odometry msgs) {
    this->pose  = msgs.pose.pose;
}

void PointRobot::sonarCallback(const p2os_msgs::SonarArray msgs) {
    this->sonar_data = msgs;
    *sonar_change = false;
}   

void PointRobot::kinectCallback(const sensor_msgs::LaserScan msgs) {
    this->kinect_data = msgs;
}

    
/**
    When called, this function will determine the angular velocity that
    the PointRobot instance should take on considering the destination location
    as well as the current position and heading of the instance itself.
    @return The new angular velocity value
*/
double PointRobot::getAngularVelocity() {
    double destination_theta;
    double delta_theta;
    double x_pos            = this->pose.position.x;
    double y_pos            = this->pose.position.y;
    double dest_x           = this->destinations.front().x;
    double dest_y           = this->destinations.front().y;
    double z_orient         = this->pose.orientation.z;
    double w_orient         = this->pose.orientation.w;
    // double position_theta   = 2*atan2(pose.rientation.z, pose.orientation.w);
    double delta_x          = dest_x-x_pos;
    double delta_y          = dest_y-y_pos;

    float angle = atan2(dest_y - y_pos, dest_x - x_pos);

    double curangle = atan2(2 * (w_orient * z_orient), w_orient*w_orient - z_orient*z_orient);


    double anglediff = angle - curangle;


    if(anglediff > PI){
        anglediff -= 2*PI;
    }
    else if(anglediff < -PI){
        anglediff += 2*PI;
    }


    if(fabs(anglediff)  <= .1){
        ANGULAR_VELOCITY = .08 * DEFAULT_SPEED * anglediff/fabs(anglediff);
    }
    else{
        ANGULAR_VELOCITY = DEFAULT_SPEED * anglediff/fabs(anglediff);
    }


    // Return
    return ANGULAR_VELOCITY;
}

/**
    When called, this function is responsible for computing the forward velocity
    of the PointRobot based on its proximity to the active destination point.
    @return The forward velocity of the PointRobot
*/
double PointRobot::getForwardVelocity() {
    double x_pos        = pose.position.x;
    double y_pos        = pose.position.y;
    double dest_x       = this->destinations.front().x;
    double dest_y       = this->destinations.front().y;
    double EXT_VARIANCE = VARIANCE*5;    

    if (
        (
            x_pos >= dest_x-VARIANCE
            && x_pos <= dest_x+VARIANCE
        )
        && (
            y_pos >= dest_y-VARIANCE 
            && y_pos <= dest_y+VARIANCE
        )
    ) {
        // If the x_pos and y_pos values are in range of the destination point
        // allowing for the specified error, we can stop the PointRobot and pop
        // the active destination off of the destinations queue.           
        printf("DESTINATION REACHED: (%.2f, %.2f)\n", dest_x, dest_y);
        destinations.pop();
        return 0.0;
    }
    else if (
        (
            x_pos >= dest_x-EXT_VARIANCE
            && x_pos <= dest_x+EXT_VARIANCE
        )
        && (
            y_pos >= dest_y-EXT_VARIANCE
            && y_pos <= dest_y+EXT_VARIANCE
        )
    ) {
        // If the x_pos and y_pos values are not in range of the destination point
        // allowing for specified error, but are at close proximity to that location,
        // slow the speed of the PointRobot in order to prepare to stop.
        return DEFAULT_SPEED/2;
    }
    else {
        // Otherwise, full steam ahead.
        return DEFAULT_SPEED;
    }
}
    
/**
    The main ROS loop responsible for combining all the above functions
    in order to visit all the destination points specified by the input file.
    @param argc The program argc input
    @param argv The program argv input
*/
int PointRobot::run(int argc, char** argv, bool run_kinect, bool run_sonar) {
    // Initialization
    ros::init(argc, argv, "motion");
    ros::NodeHandle n;

    // Publish a latched value of 1 to the Motor State
    ros::Publisher motor_state = n.advertise<p2os_msgs::MotorState>("cmd_motor_state", 1000, true);
    p2os_msgs::MotorState state;
    state.state = 1;
    motor_state.publish(state);

    // Publish a latched occupancy grid / map to RVIZ
    ros::Publisher static_map = n.advertise<nav_msgs::OccupancyGrid>("static_map", 1000, true);
    nav_msgs::OccupancyGrid grid;
    std::vector<signed char> data(this->MAP_DATA, this->MAP_DATA+(this->MAP_WIDTH*this->MAP_HEIGHT));
    grid.data            = data;
    grid.info.resolution = MAP_RESOLUTION;
    grid.info.width      = this->MAP_WIDTH;
    grid.info.height     = this->MAP_HEIGHT;
    grid.info.origin.position.x = 0.0-grid.info.resolution*((double)this->MAP_WIDTH)/2;
    grid.info.origin.position.y = 0.0-grid.info.resolution*((double)this->MAP_HEIGHT)/2;
    static_map.publish(grid);

    // Test the pose.stimation pusblisher with RVIZ, the generation of points will eventually
    // be offloaded to Loaclizer.
    
    point_cloud_pub = n.advertise<geometry_msgs::PoseArray>("point_cloud", 1000);
    

    // Create a odom subscriber so that the robot can tell where it is
    ros::Subscriber vel = n.subscribe<nav_msgs::Odometry>("/r1/odom/", 1000, &PointRobot::odomCallback, this);
    ros::Subscriber kinect;
    ros::Subscriber sonar;
    if (
        run_kinect
    ) {
        kinect = n.subscribe<sensor_msgs::LaserScan>("/r1/kinect_laser/scan", 50, &PointRobot::kinectCallback, this);
    }
    if (
        run_sonar
    ) {
        sonar  = n.subscribe<p2os_msgs::SonarArray>("/r1/sonar/", 50, &PointRobot::sonarCallback, this);        
    }
    // Create the motion publisher and set the loop rate
    ros::Publisher motion = n.advertise<geometry_msgs::Twist>("/r1/cmd_vel", 1000);
    ros::Rate loop_rate(10);

    // Main event loop

    while (ros::ok()) 
    {
        geometry_msgs::Twist msg;
        // Allow the subscriber callbacks to fire
        ros::spinOnce();
        this->whereAmI();

        // If our destiinations queue is empty, try to find another location to visit
        // If we have visited all destinations, we can exit
        if (
            destinations.empty()
        ) {
            motion.publish(msg);
            break;
        }

        // Get the angular velocity of the PointRobot



        double z_orient         = this->pose.orientation.z;
        double w_orient         = this->pose.orientation.w;
        double x_pos            = this->pose.position.x;
        double y_pos            = this->pose.position.y;
        double dest_x           = this->destinations.front().x;
        double dest_y           = this->destinations.front().y;

        float angle = atan2(dest_y - y_pos, dest_x - x_pos);

        double curangle = atan2(2 * (w_orient * z_orient), w_orient*w_orient - z_orient*z_orient);


        double anglediff = angle - curangle;


        if(anglediff > PI){
            anglediff -= 2*PI;
        }
        else if(anglediff < -PI){
            anglediff += 2*PI;
        }
        msg.angular.z = this->getAngularVelocity();        
        // If we are not currently turning, calculate the forward velocity
        // of the PointRobot
        if (
            fabs(anglediff) <= .1
            // abs(msg.angular.z) == 0.0
        ) {
            msg.linear.x = this->getForwardVelocity();
        }
        // Publish our velocities to /r1/cmd_vel
        motion.publish(msg);

        // Sleep until next required action
        loop_rate.sleep();
    }
    return 0;
}

/**
    The function responsible for reading the input file into memory
    and translating it into a queue of destination points.
    @param file The name of the file to be read
*/
std::queue<dest> PointRobot::read_file(char *file) {
    std::queue<dest> destinations; 
    std::ifstream read(file);
    std::string s;
    do {
        getline(read, s);
        if (
            read.eof()
        ) {

            break;
        }
        dest d;
        std::sscanf(s.c_str(), "%lf %lf", &d.x, &d.y);
        destinations.push(d);
    } while (!read.eof());
    return destinations;
}

void PointRobot::read_image(const char *file) {
    std::ifstream read(file);
    std::string s;
    int p_val;
    int mapIdx = this->MAP_WIDTH*this->MAP_HEIGHT - this->MAP_WIDTH;
    do {
        getline(read, s);
        if (
            read.eof()
        ) {

            break;
        }
        p_val = atoi(s.c_str());
        if (p_val != 0) {
            this->MAP_DATA[mapIdx] = 0;
        }
        else {
            this->MAP_DATA[mapIdx] = 100;
        }
        mapIdx++;
        if (mapIdx % this->MAP_WIDTH == 0) {
            mapIdx -= 2*this->MAP_WIDTH;
        }
    } while (!read.eof());
}

/**
    usage error message
*/
void usage() {
    printf("rosrun hw4 mapper <input_file> [sonar] [kinect]\n");
}

/**
    main
*/
int main(int argc, char **argv) {
    bool run_kinect = false;
    bool run_sonar = false;
    if (
        argc < 3
    ) {
        usage();
        return 0;
    }
    for (int i = 2; i < argc; i++) {
        std::string arg(argv[i]);
        if (
            arg.compare("sonar") == 0
        ) {
            run_sonar = true;
        }
        else if (
            arg.compare("kinect") == 0
        ) {
            run_kinect = true;
        }
        else {
            printf("INVALID ARGUMENT: %s\n", argv[i]);
            usage();
        }
    }
    PointRobot robot (argv[1], 0.3, 0.1);
    robot.run(argc, argv, run_kinect, run_sonar);
    ros::shutdown();
}
