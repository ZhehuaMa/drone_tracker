#include "video_test_support.h"

#include <iostream>

int main(int argc, char **argv) {
    try {
        video_test::require(argc == 2, "Usage: video_fixture_generator DIRECTORY");
        const std::string root{argv[1]};
        std::filesystem::create_directories(root);
        for (int sampling : {0, 420, 422, 444}) {
            const std::string stem{root + "/" + std::to_string(sampling)};
            std::vector<std::vector<unsigned char>> frames{};
            for (int i{0}; i < 3; ++i) {
                frames.push_back(video_test::jpeg(stem + "-" + std::to_string(i) + ".jpg", 641, 479, sampling, i));
            }
            video_test::movie(stem + ".mov", frames, 641, 479, sampling == 420);
        }
        const auto first{video_test::bytes(root + "/420-0.jpg")};
        auto bad{first};
        bad.resize(40);
        video_test::movie(root + "/bad.mov", {first, bad, first});
        const auto resized{video_test::jpeg(root + "/resized.jpg", 319, 241, 422, 4)};
        video_test::movie(root + "/resize.mov", {first, resized});
        video_test::movie(root + "/long.mov", {first, first}, 641, 479, false, 0, 10000000);
        video_test::movie(root + "/offset.mov", {first, first}, 641, 479, false, 100000, 33333);
        video_test::movie(root + "/bad-artist.mov", {first}, 641, 479, false, 0, 33333, "12bad");
        video_test::movie(root + "/non-mjpeg.mov", {first}, 641, 479, false, 0, 33333, "1", AV_CODEC_ID_PNG);
        std::vector<std::vector<unsigned char>> many(90, first);
        video_test::movie(root + "/slow.mov", many);
        std::cout << "Generated deterministic JPEG/MOV fixtures in " << root << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
