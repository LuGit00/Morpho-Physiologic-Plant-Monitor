#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace fs = std::filesystem;

const std::string IMAGE_BASE_DIR = "/var/www/html/data/images/";

const double PIXEL_TO_CM_RATIO = 0.1;
const double PIXEL_AREA_TO_CM2_RATIO = 0.01;

struct MetricData {
    std::string timestamp_str;
    std::time_t timestamp_t;
    double canopy_area;
    double color_index;
    double height_hp;
    double width1;
    double width2;
    double volumetric_proxy;
};

void saveImage(const cv::Mat& img, const std::string& filename, const std::string& text_overlay = "") {
    std::string full_path = IMAGE_BASE_DIR + filename;
    cv::Mat img_to_save = img.clone();

    if (!text_overlay.empty()) {
        cv::Scalar textColor = cv::Scalar(0, 0, 0);
        if (img_to_save.channels() == 1) {
            if (cv::mean(img_to_save).val[0] > 127) {
                textColor = cv::Scalar(0);
            } else {
                textColor = cv::Scalar(255);
            }
        } else {
            cv::Scalar mean_color = cv::mean(img_to_save);
            double brightness = (mean_color.val[0] * 0.114 + mean_color.val[1] * 0.587 + mean_color.val[2] * 0.299);
            if (brightness > 180) {
                textColor = cv::Scalar(0, 0, 0);
            } else {
                textColor = cv::Scalar(255, 255, 255);
            }
        }
        cv::putText(img_to_save, text_overlay, cv::Point(10, img_to_save.rows - 10),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, textColor, 1);
    }

    if (cv::imwrite(full_path, img_to_save)) {
        std::cout << "Generated: " << full_path << std::endl;
    } else {
        std::cerr << "Error: Could not save " << full_path << std::endl;
    }
}

cv::Mat processImageToMask(const cv::Mat& input_img) {
    if (input_img.empty()) {
        std::cerr << "Warning: Input image for mask generation is empty. Returning a black placeholder mask." << std::endl;
        return cv::Mat(200, 200, CV_8UC1, cv::Scalar(0));
    }

    cv::Mat gray_img;
    if (input_img.channels() == 3) {
        cv::cvtColor(input_img, gray_img, cv::COLOR_BGR2GRAY);
    } else {
        gray_img = input_img.clone();
    }

    cv::Mat mask;
    cv::threshold(gray_img, mask, 100, 255, cv::THRESH_BINARY);
    return mask;
}

cv::Mat generateSimulated3DRender(const cv::Mat& img_x, const cv::Mat& img_y, const cv::Mat& img_z, int width, int height) {
    cv::Mat render = cv::Mat(height, width, CV_8UC3, cv::Scalar(150, 100, 50));

    cv::Scalar plant_color = cv::Scalar(0, 200, 0);
    if (!img_y.empty() && img_y.channels() == 3) {
        plant_color = cv::mean(img_y);
    }

    int ellipse_width = img_x.empty() ? width / 3 : std::min(width / 2 - 10, img_x.cols / 2);
    int ellipse_height = img_y.empty() ? height / 3 : std::min(height / 2 - 10, img_y.rows / 2);

    cv::ellipse(render, cv::Point(width / 2, height / 2),
                cv::Size(ellipse_width, ellipse_height),
                0, 0, 360, plant_color, cv::FILLED);

    int stem_thickness = img_z.empty() ? 10 : std::min(img_z.cols / 8, 20);
    cv::rectangle(render, cv::Point(width / 2 - stem_thickness / 2, height / 2),
                  cv::Point(width / 2 + stem_thickness / 2, height - 10),
                  cv::Scalar(50, 100, 150), cv::FILLED);

    cv::putText(render, "Simulated 3D Model", cv::Point(10, height - 10),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);
    return render;
}

cv::Mat processToGrayscale(const cv::Mat& input_img) {
    if (input_img.empty()) {
        std::cerr << "Warning: Input image for grayscale conversion is empty. Returning a black placeholder." << std::endl;
        return cv::Mat(200, 200, CV_8UC1, cv::Scalar(0));
    }
    cv::Mat gray_img;
    if (input_img.channels() == 3) {
        cv::cvtColor(input_img, gray_img, cv::COLOR_BGR2GRAY);
    } else {
        gray_img = input_img.clone();
    }
    return gray_img;
}

cv::Mat processToEdges(const cv::Mat& input_img) {
    if (input_img.empty()) {
        std::cerr << "Warning: Input image for edge detection is empty. Returning a black placeholder." << std::endl;
        return cv::Mat(200, 200, CV_8UC1, cv::Scalar(0));
    }
    cv::Mat gray_img = processToGrayscale(input_img);
    cv::Mat edges;
    cv::Canny(gray_img, edges, 50, 150);
    return edges;
}

cv::Mat processToGreenChannel(const cv::Mat& input_img) {
    if (input_img.empty()) {
        std::cerr << "Warning: Input image for green channel extraction is empty. Returning a black placeholder." << std::endl;
        return cv::Mat(200, 200, CV_8UC1, cv::Scalar(0));
    }
    if (input_img.channels() == 3) {
        cv::Mat bgr[3];
        cv::split(input_img, bgr);
        return bgr[1];
    } else {
        return cv::Mat(input_img.rows, input_img.cols, CV_8UC1, cv::Scalar(0));
    }
}

cv::Mat processGreenThreshold(const cv::Mat& input_img) {
    if (input_img.empty()) {
        std::cerr << "Warning: Input image for green thresholding is empty. Returning a black placeholder." << std::endl;
        return cv::Mat(200, 200, CV_8UC1, cv::Scalar(0));
    }

    cv::Mat hsv_img;
    cv::cvtColor(input_img, hsv_img, cv::COLOR_BGR2HSV);

    cv::Scalar lower_green = cv::Scalar(30, 40, 40);
    cv::Scalar upper_green = cv::Scalar(80, 255, 255);

    cv::Mat green_mask;
    cv::inRange(hsv_img, lower_green, upper_green, green_mask);

    return green_mask;
}

double calculateBinaryArea(const cv::Mat& binary_mask) {
    if (binary_mask.empty() || binary_mask.channels() != 1 || binary_mask.type() != CV_8UC1) {
        std::cerr << "Warning: Invalid binary mask for area calculation." << std::endl;
        return 0.0;
    }
    return static_cast<double>(cv::countNonZero(binary_mask));
}

double calculateMeanHueInMask(const cv::Mat& original_bgr, const cv::Mat& binary_mask) {
    if (original_bgr.empty() || binary_mask.empty() || original_bgr.channels() != 3 || binary_mask.channels() != 1) {
        std::cerr << "Warning: Invalid input for mean hue calculation." << std::endl;
        return 0.0;
    }

    cv::Mat hsv_img;
    cv::cvtColor(original_bgr, hsv_img, cv::COLOR_BGR2HSV);

    std::vector<cv::Mat> hsv_channels;
    cv::split(hsv_img, hsv_channels);
    cv::Mat hue_channel = hsv_channels[0];

    cv::Scalar mean_hue = cv::mean(hue_channel, binary_mask);
    return mean_hue.val[0];
}

void getBoundingBoxDimensions(const cv::Mat& binary_mask, double& height, double& width) {
    height = 0.0;
    width = 0.0;
    if (binary_mask.empty() || binary_mask.channels() != 1 || binary_mask.type() != CV_8UC1) {
        std::cerr << "Warning: Invalid binary mask for bounding box calculation." << std::endl;
        return;
    }

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary_mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    if (!contours.empty()) {
        double max_area = 0;
        int largest_contour_idx = -1;
        for (size_t i = 0; i < contours.size(); ++i) {
            double area = cv::contourArea(contours[i]);
            if (area > max_area) {
                max_area = area;
                largest_contour_idx = i;
            }
        }

        if (largest_contour_idx != -1) {
            cv::Rect bounding_box = cv::boundingRect(contours[largest_contour_idx]);
            height = static_cast<double>(bounding_box.height);
            width = static_cast<double>(bounding_box.width);
        }
    }
}

void writePlantMetricsToFile(int plant_id, double canopy_area, double color_index,
                             double height_hp, double width1, double width2, double volumetric_proxy,
                             const std::string& timestamp_str) {
    std::string filename = IMAGE_BASE_DIR + "plant_" + std::to_string(plant_id) + "_metrics_" + timestamp_str + ".txt";
    std::ofstream outfile(filename);

    if (outfile.is_open()) {
        outfile << "Plant ID: " << plant_id << std::endl;
        outfile << "Timestamp: " << timestamp_str << std::endl;
        outfile << "Canopy Area (Ac): " << canopy_area << " cm^2" << std::endl;
        outfile << "Color Index (Ihue): " << color_index << std::endl;
        outfile << "Height (Hp): " << height_hp << " cm" << std::endl;
        outfile << "Width 1 (W1): " << width1 << " cm" << std::endl;
        outfile << "Width 2 (W2): " << width2 << " cm" << std::endl;
        outfile << "Volumetric Proxy (Vp): " << volumetric_proxy << " cm^3" << std::endl;
        outfile.close();
        std::cout << "Generated metrics file: " << filename << std::endl;
    } else {
        std::cerr << "Error: Could not open metrics file for writing: " << filename << std::endl;
    }
}

bool parseMetricsFile(const std::string& filename, MetricData& data) {
    std::ifstream infile(filename);
    if (!infile.is_open()) {
        std::cerr << "Warning: Could not open metrics file: " << filename << std::endl;
        return false;
    }

    std::string line;
    while (std::getline(infile, line)) {
        std::string key;
        std::string value_str;
        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            key = line.substr(0, colon_pos);
            value_str = line.substr(colon_pos + 1);
            
            key.erase(0, key.find_first_not_of(" \t\n\r\f\v"));
            key.erase(key.find_last_not_of(" \t\n\r\f\v") + 1);
            value_str.erase(0, value_str.find_first_not_of(" \t\n\r\f\v"));
            value_str.erase(value_str.find_last_not_of(" \t\n\r\f\v") + 1);

            if (key == "Timestamp") {
                data.timestamp_str = value_str;
                std::tm t = {};
                std::istringstream ss_time(value_str);
                ss_time >> std::get_time(&t, "%Y%m%d_%H%M%S");
                data.timestamp_t = std::mktime(&t);
            } else if (key == "Canopy Area (Ac)") {
                data.canopy_area = std::stod(value_str.substr(0, value_str.find(" cm^2")));
            } else if (key == "Color Index (Ihue)") {
                data.color_index = std::stod(value_str);
            } else if (key == "Height (Hp)") {
                data.height_hp = std::stod(value_str.substr(0, value_str.find(" cm")));
            } else if (key == "Width 1 (W1)") {
                data.width1 = std::stod(value_str.substr(0, value_str.find(" cm")));
            } else if (key == "Width 2 (W2)") {
                data.width2 = std::stod(value_str.substr(0, value_str.find(" cm")));
            } else if (key == "Volumetric Proxy (Vp)") {
                data.volumetric_proxy = std::stod(value_str.substr(0, value_str.find(" cm^3")));
            }
        }
    }
    infile.close();
    return true;
}

void collectHistoricalMetrics(int plant_id, std::vector<MetricData>& history_data) {
    history_data.clear();
    std::string plant_id_prefix = "plant_" + std::to_string(plant_id) + "_metrics_";

    for (const auto& entry : fs::directory_iterator(IMAGE_BASE_DIR)) {
        if (entry.is_regular_file()) {
            std::string filename = entry.path().filename().string();
            if (filename.rfind(plant_id_prefix, 0) == 0 && 
                filename.length() >= plant_id_prefix.length() + 4 && 
                filename.substr(filename.length() - 4) == ".txt") {
                MetricData data;
                if (parseMetricsFile(entry.path().string().c_str(), data)) {
                    history_data.push_back(data);
                }
            }
        }
    }
    std::sort(history_data.begin(), history_data.end(), [](const MetricData& a, const MetricData& b) {
        return a.timestamp_t < b.timestamp_t;
    });
}

void plotMetricGraph(int plant_id, const std::vector<MetricData>& history_data,
                     const std::string& metric_key_original, const std::string& graph_title,
                     const std::string& y_axis_label) {
    if (history_data.empty()) {
        std::cerr << "No historical data to plot for " << metric_key_original << " for Plant ID: " << plant_id << std::endl;
        return;
    }

    std::vector<double> values;
    for (const auto& data : history_data) {
        if (metric_key_original == "Canopy Area (Ac)") values.push_back(data.canopy_area);
        else if (metric_key_original == "Color Index (Ihue)") values.push_back(data.color_index);
        else if (metric_key_original == "Height (Hp)") values.push_back(data.height_hp);
        else if (metric_key_original == "Width 1 (W1)") values.push_back(data.width1);
        else if (metric_key_original == "Width 2 (W2)") values.push_back(data.width2);
        else if (metric_key_original == "Volumetric Proxy (Vp)") values.push_back(data.volumetric_proxy);
        else values.push_back(0.0);
    }

    if (values.empty()) {
        std::cerr << "No valid values found for metric: " << metric_key_original << std::endl;
        return;
    }

    std::string sanitized_metric_key = metric_key_original;
    std::replace(sanitized_metric_key.begin(), sanitized_metric_key.end(), ' ', '_');
    std::replace(sanitized_metric_key.begin(), sanitized_metric_key.end(), '(', '_');
    std::replace(sanitized_metric_key.begin(), sanitized_metric_key.end(), ')', '_');
    size_t pos;
    while ((pos = sanitized_metric_key.find("__")) != std::string::npos) {
        sanitized_metric_key.replace(pos, 2, "_");
    }
    if (sanitized_metric_key.back() == '_') {
        sanitized_metric_key.pop_back();
    }


    int graph_width = 800;
    int graph_height = 600;
    int margin_x = 80;
    int margin_y = 80;
    int plot_width = graph_width - 2 * margin_x;
    int plot_height = graph_height - 2 * margin_y;

    cv::Mat graph_img(graph_height, graph_width, CV_8UC3, cv::Scalar(255, 255, 255));

    cv::line(graph_img, cv::Point(margin_x, margin_y), cv::Point(margin_x, graph_height - margin_y), cv::Scalar(0, 0, 0), 2);
    cv::line(graph_img, cv::Point(margin_x, graph_height - margin_y), cv::Point(graph_width - margin_x, graph_height - margin_y), cv::Scalar(0, 0, 0), 2);

    double min_val = *std::min_element(values.begin(), values.end());
    double max_val = *std::max_element(values.begin(), values.end());
    if (max_val == min_val) {
        max_val += 1.0;
        min_val -= 1.0;
    }
    double y_scale = plot_height / (max_val - min_val);

    cv::Point prev_point(-1, -1);
    for (size_t i = 0; i < values.size(); ++i) {
        int x = margin_x + static_cast<int>(i * plot_width / (values.size() > 1 ? (values.size() - 1) : 1));
        int y = graph_height - margin_y - static_cast<int>((values[i] - min_val) * y_scale);

        cv::circle(graph_img, cv::Point(x, y), 3, cv::Scalar(255, 0, 0), -1);

        if (prev_point.x != -1) {
            cv::line(graph_img, prev_point, cv::Point(x, y), cv::Scalar(0, 0, 255), 1);
        }
        prev_point = cv::Point(x, y);
    }

    int label_interval = std::max(1, (int)(values.size() / 5));
    for (size_t i = 0; i < values.size(); i += label_interval) {
        int x = margin_x + static_cast<int>(i * plot_width / (values.size() > 1 ? (values.size() - 1) : 1));
        std::string label_text = history_data[i].timestamp_str.substr(4, 4);
        cv::putText(graph_img, label_text, cv::Point(x - 15, graph_height - margin_y + 20), cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(0, 0, 0), 1);
    }
    cv::putText(graph_img, "Time (YYYYMMDD_HHMMSS)", cv::Point(graph_width / 2 - 50, graph_height - margin_y + 40), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0), 1);


    cv::putText(graph_img, std::to_string(static_cast<int>(min_val)), cv::Point(margin_x - 40, graph_height - margin_y + 5), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0), 1);
    cv::putText(graph_img, std::to_string(static_cast<int>(max_val)), cv::Point(margin_x - 40, margin_y + 5), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0), 1);
    cv::putText(graph_img, y_axis_label, cv::Point(10, graph_height / 2), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0), 1);

    cv::putText(graph_img, graph_title, cv::Point(graph_width / 2 - 100, 30), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 0, 0), 2);

    std::string output_filename = IMAGE_BASE_DIR + "plant_" + std::to_string(plant_id) + "_" + sanitized_metric_key + "_graph.png";
    if (cv::imwrite(output_filename, graph_img)) {
        std::cout << "Generated graph image: " << output_filename << std::endl;
    } else {
        std::cerr << "Error: Could not save graph image: " << output_filename << std::endl;
    }
}


int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <plant_id>" << std::endl;
        std::cerr << "Example: " << argv[0] << " 1" << std::endl;
        return 1;
    }

    int plant_id = std::stoi(argv[1]);
    if (plant_id <= 0) {
        std::cerr << "Error: Plant ID must be a positive integer." << std::endl;
        return 1;
    }

    auto now = std::chrono::system_clock::now();
    std::time_t current_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm* local_tm = std::localtime(&current_time_t);
    std::stringstream ss;
    ss << std::put_time(local_tm, "%Y%m%d_%H%M%S");
    std::string timestamp_str = ss.str();

    if (!fs::exists(IMAGE_BASE_DIR)) {
        if (fs::create_directories(IMAGE_BASE_DIR)) {
            std::cout << "Created directory: " << IMAGE_BASE_DIR << std::endl;
        } else {
            std::cerr << "Error: Could not create directory " << IMAGE_BASE_DIR << std::endl;
            return 1;
        }
    }

    std::string plant_id_str = std::to_string(plant_id);
    int img_width = 200;
    int img_height = 200;

    cv::Mat initial_x_img = cv::imread(IMAGE_BASE_DIR + "plant_" + plant_id_str + "_initial_X.jpg");
    cv::Mat initial_y_img = cv::imread(IMAGE_BASE_DIR + "plant_" + plant_id_str + "_initial_Y.jpg");
    cv::Mat initial_z_img = cv::imread(IMAGE_BASE_DIR + "plant_" + plant_id_str + "_initial_Z.jpg");

    if (!initial_x_img.empty()) {
        img_width = initial_x_img.cols;
        img_height = initial_x_img.rows;
    } else if (!initial_y_img.empty()) {
        img_width = initial_y_img.cols;
        img_height = initial_y_img.rows;
    } else if (!initial_z_img.empty()) {
        img_width = initial_z_img.cols;
        img_height = initial_z_img.rows;
    }

    if (initial_x_img.empty()) {
        std::cerr << "Warning: plant_" << plant_id_str << "_initial_X.jpg not found or could not be read. Generating placeholder for X-axis input." << std::endl;
        initial_x_img = cv::Mat(img_height, img_width, CV_8UC3, cv::Scalar(100, 100, 200));
        cv::putText(initial_x_img, "No X Input", cv::Point(10, img_height / 2), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 2);
    } else {
        if (initial_x_img.cols != img_width || initial_x_img.rows != img_height) {
            cv::resize(initial_x_img, initial_x_img, cv::Size(img_width, img_height));
        }
    }
    if (initial_y_img.empty()) {
        std::cerr << "Warning: plant_" << plant_id_str << "_initial_Y.jpg not found or could not be read. Generating placeholder for Y-axis input." << std::endl;
        initial_y_img = cv::Mat(img_height, img_width, CV_8UC3, cv::Scalar(100, 200, 100));
        cv::putText(initial_y_img, "No Y Input", cv::Point(10, img_height / 2), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 2);
    } else {
        if (initial_y_img.cols != img_width || initial_y_img.rows != img_height) {
            cv::resize(initial_y_img, initial_y_img, cv::Size(img_width, img_height));
        }
    }
    if (initial_z_img.empty()) {
        std::cerr << "Warning: plant_" << plant_id_str << "_initial_Z.jpg not found or could not be read. Generating placeholder for Z-axis input." << std::endl;
        initial_z_img = cv::Mat(img_height, img_width, CV_8UC3, cv::Scalar(200, 100, 100));
        cv::putText(initial_z_img, "No Z Input", cv::Point(10, img_height / 2), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 2);
    } else {
        if (initial_z_img.cols != img_width || initial_z_img.rows != img_height) {
            cv::resize(initial_z_img, initial_z_img, cv::Size(img_width, img_height));
        }
    }

    cv::Mat top_green_filtered_img = processGreenThreshold(initial_y_img);
    saveImage(processImageToMask(initial_y_img), "plant_" + plant_id_str + "_top_mask.jpg", "Top Mask (Processed)");
    saveImage(processToGrayscale(initial_y_img), "plant_" + plant_id_str + "_top_grayscale.jpg", "Top Grayscale");
    saveImage(processToEdges(initial_y_img), "plant_" + plant_id_str + "_top_edges.jpg", "Top Edges");
    saveImage(processToGreenChannel(initial_y_img), "plant_" + plant_id_str + "_top_green.jpg", "Top Green Ch.");
    saveImage(top_green_filtered_img, "plant_" + plant_id_str + "_top_green_filtered.jpg", "Top Green Filtered");

    cv::Mat side1_green_filtered_img = processGreenThreshold(initial_x_img);
    saveImage(processImageToMask(initial_x_img), "plant_" + plant_id_str + "_side1_mask.jpg", "Side 1 Mask (Processed)");
    saveImage(processToGrayscale(initial_x_img), "plant_" + plant_id_str + "_side1_grayscale.jpg", "Side 1 Grayscale");
    saveImage(processToEdges(initial_x_img), "plant_" + plant_id_str + "_side1_edges.jpg", "Side 1 Edges");
    saveImage(processToGreenChannel(initial_x_img), "plant_" + plant_id_str + "_side1_green.jpg", "Side 1 Green Ch.");
    saveImage(side1_green_filtered_img, "plant_" + plant_id_str + "_side1_green_filtered.jpg", "Side 1 Green Filtered");
    
    cv::Mat side2_green_filtered_img = processGreenThreshold(initial_z_img);
    saveImage(processImageToMask(initial_z_img), "plant_" + plant_id_str + "_side2_mask.jpg", "Side 2 Mask (Processed)");
    saveImage(processToGrayscale(initial_z_img), "plant_" + plant_id_str + "_side2_grayscale.jpg", "Side 2 Grayscale");
    saveImage(processToEdges(initial_z_img), "plant_" + plant_id_str + "_side2_edges.jpg", "Side 2 Edges");
    saveImage(processToGreenChannel(initial_z_img), "plant_" + plant_id_str + "_side2_green.jpg", "Side 2 Green Ch.");
    saveImage(side2_green_filtered_img, "plant_" + plant_id_str + "_side2_green_filtered.jpg", "Side 2 Green Filtered");

    saveImage(generateSimulated3DRender(initial_x_img, initial_y_img, initial_z_img, img_width, img_height),
              "plant_" + plant_id_str + "_3d_render.png", "");

    double canopy_area = calculateBinaryArea(top_green_filtered_img) * PIXEL_AREA_TO_CM2_RATIO;
    double color_index = calculateMeanHueInMask(initial_y_img, top_green_filtered_img);

    double height_hp = 0.0, width1 = 0.0, width2 = 0.0;
    getBoundingBoxDimensions(side1_green_filtered_img, height_hp, width1);
    height_hp *= PIXEL_TO_CM_RATIO;
    width1 *= PIXEL_TO_CM_RATIO;

    getBoundingBoxDimensions(side2_green_filtered_img, height_hp, width2);
    width2 *= PIXEL_TO_CM_RATIO;

    double volumetric_proxy = canopy_area * height_hp * 0.5;

    writePlantMetricsToFile(plant_id, canopy_area, color_index, height_hp, width1, width2, volumetric_proxy, timestamp_str);

    std::vector<MetricData> history_data;
    collectHistoricalMetrics(plant_id, history_data);

    if (!history_data.empty()) {
        std::cout << "Generating historical graphs for Plant ID: " << plant_id << std::endl;
        plotMetricGraph(plant_id, history_data, "Canopy Area (Ac)", "Canopy Area Over Time", "Canopy Area (cm^2)");
        plotMetricGraph(plant_id, history_data, "Color Index (Ihue)", "Color Index Over Time", "Color Index");
        plotMetricGraph(plant_id, history_data, "Height (Hp)", "Plant Height Over Time", "Height (cm)");
        plotMetricGraph(plant_id, history_data, "Width 1 (W1)", "Width 1 Over Time", "Width (cm)");
        plotMetricGraph(plant_id, history_data, "Width 2 (W2)", "Width 2 Over Time", "Width (cm)");
        plotMetricGraph(plant_id, history_data, "Volumetric Proxy (Vp)", "Volumetric Proxy Over Time", "Volume (cm^3)");
    } else {
        std::cerr << "Not enough historical data to generate graphs for Plant ID: " << plant_id << std::endl;
    }

    std::cout << "Image generation and graphing complete for Plant ID: " << plant_id << std::endl;

    return 0;
}
