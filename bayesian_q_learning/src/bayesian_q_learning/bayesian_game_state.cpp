#include "bayesian_q_learning/bayesian_game_state.h"

#include "pacman_abstract_classes/util_functions.h"
#include <boost/math/special_functions/round.hpp>


BayesianGameState::BayesianGameState()
{
    pacman_observer_service_ = n_.advertiseService<pacman_msgs::AgentPoseService::Request, pacman_msgs::AgentPoseService::Response>
                                ("/pacman/pacman_pose/error", boost::bind(&BayesianGameState::observeAgent, this, _1, _2));
    ghost_distance_observer_service_ = n_.advertiseService<pacman_msgs::AgentPoseService::Request, pacman_msgs::AgentPoseService::Response>
                                ("/pacman/ghost_distance/error", boost::bind(&BayesianGameState::observeAgent, this, _1, _2));

    precalculateAllDistances();
    ROS_DEBUG_STREAM("Bayesian game state initialized");
}

BayesianGameState::~BayesianGameState()
{
    pacman_observer_service_.shutdown();
    ghost_distance_observer_service_.shutdown();

    precalculated_distances_.clear();

    ROS_INFO_STREAM("Bayesian game state destroyed");
}

void BayesianGameState::observePacman(double measurement_x, double measurement_y)
{
    float SD_PACMAN_MEASUREMENT = 0.5;

    std::vector<float> pacman_pose_map_line (width_, 0);
    std::vector< std::vector<float> > pacman_new_pose_map (height_, pacman_pose_map_line);
    float sum_probabilities = 0.0;

    for (int i = 0 ; i < width_ ; i++)
    {
        for (int j = 0 ; j < height_ ; j++)
        {
            if( map_[j][i] != WALL)
            {
                float old_probability_of_being_in_this_place = pacman_pose_map_[j][i];
                float probability_of_place_given_z = util::getProbOfMeasurementGivenPosition(i, j, measurement_x, measurement_y, SD_PACMAN_MEASUREMENT);

                //ROS_INFO_STREAM("" << random_probability);
                pacman_new_pose_map[j][i] = probability_of_place_given_z * old_probability_of_being_in_this_place;

                sum_probabilities += pacman_new_pose_map[j][i];
            }
        }
    }

    for (int i = 0 ; i < width_ ; i++)
    {
        for (int j = 0 ; j < height_ ; j++)
        {
            if( map_[j][i] != WALL)
            {
                if(sum_probabilities != 0)
                    pacman_new_pose_map[j][i] = pacman_new_pose_map[j][i]/sum_probabilities;
                else
                {
                    ROS_WARN_STREAM_THROTTLE(1, "Probability 0 for pacman, redistributing");
                    pacman_new_pose_map[j][i] = 1.0 / (float) ( height_ * width_ );
                }
            }
        }
    }

    pacman_pose_map_.clear();
    pacman_pose_map_ = pacman_new_pose_map;

    int measurement_x_int = boost::math::iround(measurement_x);
    int measurement_y_int = boost::math::iround(measurement_y);

    if( measurement_x_int > 0 && measurement_x_int < width_ && 
        measurement_y_int > 0 && measurement_y_int < height_ && 
        map_[measurement_y_int][measurement_x_int] != WALL)
    {
        map_[measurement_y_int][measurement_x_int] = EMPTY;
    }

    //ROS_WARN_STREAM("PAcman pose: x " << measurement_x << " y " << measurement_y);
    //printPacmanOrGhostPose(true, 0);
    //printDeterministicMap();
}

void BayesianGameState::observeGhost(double measurement_x_dist, double measurement_y_dist, int ghost_index)
{
    double SD_GHOST_DIST_MEASUREMENT = 0.5;

    std::vector< std::vector<float> > ghost_pose_map = ghosts_poses_map_[ghost_index];

    std::vector<float> ghost_pose_map_line (width_, 0);
    std::vector< std::vector<float> > ghost_new_pose_map (height_, ghost_pose_map_line);
    float sum_probabilities = 0.0;

    for (int i = 0 ; i < width_ ; i++)
    {
        for (int j = 0 ; j < height_ ; j++)
        {
            if( map_[j][i] != WALL)
            {
                float old_probability_of_being_in_this_place = ghost_pose_map[j][i];
                for (int pacman_i = 0 ; pacman_i < width_ ; pacman_i++)
                {
                    for (int pacman_j = 0 ; pacman_j < height_ ; pacman_j++)
                    {
                        float probability_of_pacman = pacman_pose_map_[pacman_j][pacman_i];
                        float probability_of_place_given_z = util::getProbOfMeasurementGivenPosition(i - pacman_i, j - pacman_j, measurement_x_dist, measurement_y_dist, SD_GHOST_DIST_MEASUREMENT);

                        //ROS_INFO_STREAM("" << random_probability);
                        ghost_new_pose_map[j][i] += probability_of_place_given_z * old_probability_of_being_in_this_place * probability_of_pacman;
                    }
                }
                sum_probabilities += ghost_new_pose_map[j][i];
            }
        }
    }

    if(sum_probabilities == 0)
    {
        ROS_WARN_STREAM("Probability 0 for ghost " << ghost_index << ", redistributing");
    }

    for (int i = 0 ; i < width_ ; i++)
    {
        for (int j = 0 ; j < height_ ; j++)
        {
            if( map_[j][i] != WALL)
            {
                if(sum_probabilities != 0)
                    ghost_new_pose_map[j][i] = ghost_new_pose_map[j][i]/sum_probabilities;
                else
                    ghost_new_pose_map[j][i] = 1.0 / (float) ( height_ * width_ );
            }
        }
    }

    ghosts_poses_map_[ghost_index].clear();
    ghosts_poses_map_[ghost_index] = ghost_new_pose_map;
}

bool BayesianGameState::observeAgent(pacman_msgs::AgentPoseService::Request &req, pacman_msgs::AgentPoseService::Response &res)
{
    int agent = (int) req.agent;
    geometry_msgs::Pose pose = (geometry_msgs::Pose) req.pose;
    double measurement_x = pose.position.x;
    double measurement_y = pose.position.y;

    is_finished_ = (bool) req.is_finished;

    if(agent == pacman_msgs::AgentPoseService::Request::PACMAN)
    {
        ROS_INFO_STREAM("Observe pacman");
        observePacman(measurement_x, measurement_y);
    }
    else
    {
        int ghost_index = agent - 1;
        ROS_INFO_STREAM("Observe ghost " << ghost_index);
        observeGhost(measurement_x, measurement_y, ghost_index);
    }

    res.observed = true;
    return true;
}

void BayesianGameState::predictPacmanMove(pacman_msgs::PacmanAction action)
{
    ROS_INFO_STREAM("Predict pacman");

    std::vector<float> map_line (width_, 0);
    std::vector< std::vector<float> > pacman_new_pose_map (height_, map_line);
    std::vector< std::vector<float> > new_foods_map (height_, map_line);
    map_line.clear();

    for (int i = 0 ; i < width_ ; i++)
    {
        for (int j = 0 ; j < height_ ; j++)
        {
            if( map_[j][i] != WALL)
            {
                float probability_of_being_in_this_place = pacman_pose_map_[j][i];

                std::vector< std::pair< float, std::pair<int, int> > > next_positions = getNextPositionsForActionWithProbabilities(i, j, action);

                //ROS_INFO_STREAM("" << random_probability);

                for(std::vector< std::pair< float, std::pair<int, int> > >::reverse_iterator it = next_positions.rbegin(); it != next_positions.rend(); ++it)
                {
                    /* std::cout << *it; ... */
                    float probability_of_move = it->first;
                    int x = it->second.first;
                    int y = it->second.second;
                    pacman_new_pose_map[y][x] += probability_of_move * probability_of_being_in_this_place;


                }
            }
        }
    }

    for (int i = 0 ; i < width_ ; i++)
    {
        for (int j = 0 ; j < height_ ; j++)
        {
            if( map_[j][i] != WALL)
            {
                new_foods_map[j][i] = foods_map_[j][i] * ( 1 - pacman_new_pose_map[j][i]);
            }
        }
    }

    pacman_pose_map_.clear();
    pacman_pose_map_ = pacman_new_pose_map;

    foods_map_.clear();
    foods_map_ = new_foods_map;

    //printPacmanOrGhostPose(true, 0);
    //ROS_INFO_STREAM("Foods map");
    //printFoodsMap();
}

void BayesianGameState::predictGhostMove(int ghost_index)
{
    ROS_INFO_STREAM("Predict ghost " << ghost_index);

    double STOP_PROBABILITY = 0.2;

    std::vector<float> ghost_pose_map_line (width_, 0);
    std::vector< std::vector<float> > ghost_new_pose_map (height_, ghost_pose_map_line);

    std::vector< std::vector<float> > ghost_pose_map = ghosts_poses_map_[ghost_index];

    for (int i = 0 ; i < width_ ; i++)
    {
        for (int j = 0 ; j < height_ ; j++)
        {
            if( map_[j][i] != WALL)
            {
                float probability_of_being_in_this_place = ghost_pose_map[j][i];

                std::vector< std::pair<int, int> > next_positions = getLegalNextPositions(i, j);
                float random_probability = (1.0 - STOP_PROBABILITY)/next_positions.size();

                ghost_new_pose_map[j][i] += STOP_PROBABILITY * probability_of_being_in_this_place;

                //ROS_INFO_STREAM("" << random_probability);

                for(std::vector< std::pair<int, int> >::reverse_iterator it = next_positions.rbegin(); it != next_positions.rend(); ++it)
                {
                    /* std::cout << *it; ... */
                    int x = it->first;
                    int y = it->second;
                    ghost_new_pose_map[y][x] += random_probability * probability_of_being_in_this_place;
                }
            }
        }
    }

    ghosts_poses_map_[ghost_index].clear();
    ghosts_poses_map_[ghost_index] = ghost_new_pose_map;
}

void BayesianGameState::predictGhostsMoves()
{
    for(int i = 0 ; i < num_ghosts_ ; ++i)
    {
        predictGhostMove(i);
    }
}

void BayesianGameState::predictAgentsMoves(pacman_msgs::PacmanAction action)
{
    predictPacmanMove(action);
    predictGhostsMoves();
}

bool BayesianGameState::isFinished()
{
    return is_finished_;
}

// TODO: dividing by map width*height
float BayesianGameState::getClosestFoodDistance()
{
    geometry_msgs::Pose new_pose = getMostProbablePacmanPose();
    int new_x = new_pose.position.x;
    int new_y = new_pose.position.y;

    float food_probability_threshold = getMaxFoodProbability()/2.0;

    std::map< std::pair<int, int>, int > distances = getDistances(new_x, new_y);

    int min_dist = util::INFINITE;

    for (int i = 0 ; i < height_ ; i++) {
        for (int j = 0 ; j < width_ ; j++) {
            if(foods_map_[i][j] >= food_probability_threshold) {
                int dist = distances[std::make_pair(j, i)];
                if (dist < min_dist) {
                    min_dist = dist;
                }
            }
        }
    }

    return min_dist;// / ( (float) height_ * width_);
}

bool BayesianGameState::eatsFood(pacman_msgs::PacmanAction action)
{
    geometry_msgs::Pose new_pose = getMostProbablePacmanPose();
    int new_x = new_pose.position.x;
    int new_y = new_pose.position.y;

    if(map_[new_y][new_x] == FOOD)
        return true;
    return false;
}

int BayesianGameState::getClosestGhostDistance()
{
    geometry_msgs::Pose new_pose = getPacmanPose();
    int new_x = new_pose.position.x;
    int new_y = new_pose.position.y;
    std::vector< geometry_msgs::Pose > ghosts_poses = getMostProbableGhostsPoses();

    std::map< std::pair<int, int>, int > distances = getDistances(new_x, new_y);

    int min_dist = util::INFINITE;

    for(std::vector< geometry_msgs:: Pose >::reverse_iterator it = ghosts_poses.rbegin();
                             it != ghosts_poses.rend() ; ++it)
    {
        int dist = distances[std::make_pair(it->position.x, it->position.y)];

        if (dist < min_dist) {
            min_dist = dist;
        }
    }

    if (min_dist == 0)
        min_dist = 1;

    return min_dist;// / ( (float) height_ * width_);
}

bool BayesianGameState::dies(pacman_msgs::PacmanAction action)
{
    geometry_msgs::Pose new_pose = getPacmanPose();
    int new_x = new_pose.position.x;
    int new_y = new_pose.position.y;

    for(std::vector< geometry_msgs:: Pose >::reverse_iterator it = ghosts_poses_.rbegin();
                             it != ghosts_poses_.rend() ; ++it)
    {
        if ( (new_x == it->position.x) && (new_y == it->position.y) )
            return true;
    }

    return false;
}

int BayesianGameState::getNumberOfGhostsOneStepAway(pacman_msgs::PacmanAction action)
{
    geometry_msgs::Pose new_pose = getPacmanPose();
    int new_x = new_pose.position.x;
    int new_y = new_pose.position.y;

    int number_ghost = 0;

    for(std::vector< geometry_msgs:: Pose >::reverse_iterator it = ghosts_poses_.rbegin();
                             it != ghosts_poses_.rend() ; ++it)
    {
        int dist = abs(new_x - it->position.x) + abs(new_y - it->position.y);

        if (dist <= 1) {
            number_ghost++;
        }
    }

    return number_ghost;
}

int BayesianGameState::getNumberOfGhostsNStepsAway(int n)
{
    geometry_msgs::Pose pacman_pose = getMostProbablePacmanPose();
    std::vector< geometry_msgs::Pose > ghosts_poses = getMostProbableGhostsPoses();
    int new_x = pacman_pose.position.x;
    int new_y = pacman_pose.position.y;

    std::map< std::pair<int, int>, int > distances = getDistances(new_x, new_y);

    int number_ghost = 0;

    for(std::vector< geometry_msgs:: Pose >::reverse_iterator it = ghosts_poses.rbegin();
                             it != ghosts_poses.rend() ; ++it)
    {
        //int dist = abs(new_x - it->position.x) + abs(new_y - it->position.y);
        int dist = distances[std::make_pair(it->position.x, it->position.y)];

        if (dist <= n) {
            number_ghost++;
        }
    }

    return number_ghost;
}

bool BayesianGameState::hasGhostNStepsAway(int n)
{
    bool has_ghost = ( getNumberOfGhostsNStepsAway(n) > 0 );
    return has_ghost;
}

float BayesianGameState::getProbabilityOfAGhostNStepsAway(int n)
{
    double probability = 0;

    for (int i = 0 ; i < width_ ; i++)
    {
        for (int j = 0 ; j < height_ ; j++)
        {
            if( map_[j][i] != WALL)
            {
                float probability_of_being_in_this_place = pacman_pose_map_[j][i];
                std::map< std::pair<int, int>, int > distances = getDistances(i, j);

                for (int ghost_i = 0 ; ghost_i < width_ ; ghost_i++)
                {
                    for (int ghost_j = 0 ; ghost_j < height_ ; ghost_j++)
                    {
                        int dist = distances[std::make_pair(ghost_i, ghost_j)];

                        if( map_[ghost_j][ghost_i] != WALL && dist < n)
                        {
                            for (int ghost_index = 0 ; ghost_index < num_ghosts_ ; ghost_index++)
                            {
                                probability += probability_of_being_in_this_place * ghosts_poses_map_[ghost_index][ghost_j][ghost_i];
                            }
                        }
                    }
                }
            }
        }
    }

    return probability;
}

std::map< std::pair<int, int>, int > BayesianGameState::calculateDistances(int x, int y)
{
    std::map< std::pair<int, int>, int > distances;

    distances[std::make_pair(x, y)] = 0;

    int current_distance = 0;
    bool done = false;

    std::vector< std::pair<int, int> > legal_positions = getLegalNextPositions(x, y);

    while(!done)
    {
        done = true;
        current_distance++;
        std::vector< std::pair<int, int> > next_legal_positions;

        for(std::vector< std::pair<int, int> >::reverse_iterator it = legal_positions.rbegin(); it != legal_positions.rend(); ++it)
        {
            if ( distances.find(*it) == distances.end() )
            {
                distances[*it] = current_distance;
                done = false;

                std::vector< std::pair<int, int> > temp_legal_positions = getLegalNextPositions(it->first, it->second);
                next_legal_positions.reserve(next_legal_positions.size() + temp_legal_positions.size());
                next_legal_positions.insert(next_legal_positions.end(), temp_legal_positions.begin(), temp_legal_positions.end());
            }
        }

        legal_positions = next_legal_positions;
    }
    
    return distances;
}

void BayesianGameState::precalculateAllDistances()
{
    ROS_DEBUG_STREAM("Pre-calculating all distances");

    for (int i = 0 ; i < width_ ; i++)
    {
        for (int j = 0 ; j < height_ ; j++)
        {
            if( map_[j][i] != WALL )
            {
                precalculated_distances_[std::make_pair(i, j)] = calculateDistances(i, j);
            }
        }
    }

    ROS_DEBUG_STREAM("Pre-calculated all distances");
}

std::map< std::pair<int, int>, int > BayesianGameState::getDistances(int x, int y)
{
    std::map< std::pair<int, int>, int > distances = precalculated_distances_[std::make_pair(x, y)];
    
    return distances;
}

float BayesianGameState::getMaxFoodProbability()
{
    double max_probability = -util::INFINITE;

    for (int i = 0 ; i < width_ ; i++)
    {
        for (int j = 0 ; j < height_ ; j++)
        {
            double probability = this->foods_map_[j][i];
            if (probability > max_probability)
            {
                max_probability = probability;
            }
        }
    }

    return max_probability;
}

geometry_msgs::Pose BayesianGameState::getMostProbablePacmanPose()
{
    geometry_msgs::Pose probable_pose;

    int probable_x = -1;
    int probable_y = -1;
    double max_probability = -util::INFINITE;

    for (int i = 0 ; i < width_ ; i++)
    {
        for (int j = 0 ; j < height_ ; j++)
        {
            double probability = this->pacman_pose_map_[j][i];
            if (probability > max_probability)
            {
                probable_x = i;
                probable_y = j;

                max_probability = probability;
            }
        }
    }

    probable_pose.position.x = probable_x;
    probable_pose.position.y = probable_y;

    return probable_pose;
}

geometry_msgs::Pose BayesianGameState::getMostProbableGhostPose(int ghost_index)
{
    geometry_msgs::Pose probable_pose;

    int probable_x = -1;
    int probable_y = -1;
    double max_probability = -util::INFINITE;

    for (int i = 0 ; i < width_ ; i++)
    {
        for (int j = 0 ; j < height_ ; j++)
        {
            double probability = this->ghosts_poses_map_[ghost_index][j][i];
            if (probability > max_probability)
            {
                probable_x = i;
                probable_y = j;

                max_probability = probability;
            }
        }
    }

    probable_pose.position.x = probable_x;
    probable_pose.position.y = probable_y;

    return probable_pose;
}

std::vector< geometry_msgs::Pose > BayesianGameState::getMostProbableGhostsPoses()
{
    std::vector< geometry_msgs::Pose > probable_poses;

    for(int i = 0 ; i < num_ghosts_ ; ++i)
    {
        probable_poses.push_back(getMostProbableGhostPose(i));
    }

    return probable_poses;
}

// TODO: update food probabilities