"""RK3576 实时本地 MJPEG 视频入口；算法参数完全来自 params_file。"""
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
                FindPackageShare('drone_tracker'), 'parameters', 'rk3576_video_params.yaml']),
            description='已填写模型路径及当前视频内参的参数文件'),
        DeclareLaunchArgument('video_source', description='本地 MJPEG 视频路径，推荐绝对路径'),
        Node(package='drone_tracker', executable='drone_tracker_node', output='screen',
             parameters=[LaunchConfiguration('params_file'),
                         {'video_source': LaunchConfiguration('video_source')}]),
    ])
