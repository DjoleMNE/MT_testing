#include <kdl_eigen_conversions.hpp>

// Convert from KDL vector to a 3x1 eigen vector
Eigen::Vector3d kdl_vector_to_eigen(const KDL::Vector &kdl_vector)
{
    return Eigen::Vector3d(kdl_vector(0), kdl_vector(1), kdl_vector(2));
}

// Convert from KDL twist to a 6x1 eigen vector/matrix
void kdl_twist_to_eigen(const KDL::Twist &kdl_twist, Eigen::VectorXd &eigen_vector)
{
    assert(eigen_vector.rows() == 6);
    for(int i = 0; i < 6; i++) eigen_vector(i) = kdl_twist(i);
}

void rotation_to_eigen(const KDL::Rotation &kdl_matrix, Eigen::Matrix3d &eigen_matrix)
{
    eigen_matrix(0, 0) = kdl_matrix.data[0];
    eigen_matrix(0, 1) = kdl_matrix.data[1];
    eigen_matrix(0, 2) = kdl_matrix.data[2];
    eigen_matrix(1, 0) = kdl_matrix.data[3];
    eigen_matrix(1, 1) = kdl_matrix.data[4];
    eigen_matrix(1, 2) = kdl_matrix.data[5];
    eigen_matrix(2, 0) = kdl_matrix.data[6];
    eigen_matrix(2, 1) = kdl_matrix.data[7];
    eigen_matrix(2, 2) = kdl_matrix.data[8];
}

void eigen_to_rotation(const Eigen::Matrix3d &eigen_matrix, KDL::Rotation &kdl_matrix)
{
    kdl_matrix.data[0] = eigen_matrix(0, 0);
    kdl_matrix.data[1] = eigen_matrix(0, 1);
    kdl_matrix.data[2] = eigen_matrix(0, 2);
    kdl_matrix.data[3] = eigen_matrix(1, 0);
    kdl_matrix.data[4] = eigen_matrix(1, 1);
    kdl_matrix.data[5] = eigen_matrix(1, 2);
    kdl_matrix.data[6] = eigen_matrix(2, 0);
    kdl_matrix.data[7] = eigen_matrix(2, 1);
    kdl_matrix.data[8] = eigen_matrix(2, 2);
}