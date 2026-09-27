"""RK3576 相机入口，支持连续和周期软件触发；算法参数完全来自 params_file。"""
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument(
            'params_file',
            default_value=PathJoinSubstitution([
                FindPackageShare('drone_tracker'), 'parameters', 'rk3576_ar0234_camera_params.yaml']),
            description='已填写模型路径及当前相机内参的参数文件'),
        DeclareLaunchArgument('video_source', description='相机 video0、video10 或绝对 /dev 设备路径'),
        Node(package='drone_tracker', executable='drone_tracker_node', output='screen',
             parameters=[LaunchConfiguration('params_file'),
                         {'video_source': LaunchConfiguration('video_source')}]),
    ])
