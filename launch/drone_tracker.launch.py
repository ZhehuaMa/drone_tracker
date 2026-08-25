import os
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.substitutions import LaunchConfiguration, TextSubstitution
from launch.actions import DeclareLaunchArgument
from launch_ros.actions import Node

def generate_launch_description():

    video_source_arg = DeclareLaunchArgument(
            'video_source',
            default_value=TextSubstitution(text='video0'),
            # default_value=TextSubstitution(text='/home/dg/Videos/ts_20251201_155214_34690.mjpeg'),
            description='Video source file or device index for the drone tracker node.'
        )

    enable_dcf_arg = DeclareLaunchArgument(
            'enable_dcf',
            default_value='true',
            description='Enable grayscale DCF fusion with NanoTrack (A/B testing).'
        )

    config = os.path.join(
        get_package_share_directory('drone_tracker'),
        'parameters',
        'drone_tracker_params.yaml'
    )

    return LaunchDescription([
        video_source_arg,
        enable_dcf_arg,
        Node(
            package='drone_tracker',
            executable='drone_tracker_node',
            name='drone_tracker_node',
            parameters=[
                config,
                {'video_source': LaunchConfiguration('video_source')},
                {'tracker.enable_dcf': LaunchConfiguration('enable_dcf')}],
            output='screen',
            emulate_tty=True,
        )
    ])
