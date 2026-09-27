#include "orbital/atomic_orbital.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMatrix4x4>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr qsizetype kMaximumPayloadBytes = 1024 * 1024;
constexpr double kProbabilityCutoff = 0.95;

struct AngularName {
    const char* suffix;
    const char* label;
    int m;
};

const std::array<QVector<AngularName>, 5>& angular_names() {
    static const std::array<QVector<AngularName>, 5> names{{
        {{"s", "s", 0}},
        {{"px", "p_x", 1}, {"py", "p_y", -1}, {"pz", "p_z", 0}},
        {{"dxy", "d_{xy}", -2}, {"dxz", "d_{xz}", 1}, {"dyz", "d_{yz}", -1},
         {"dx2-y2", "d_{x^2-y^2}", 2}, {"dz2", "d_{z^2}", 0}},
        {{"fy(3x2-y2)", "f_{y(3x^2-y^2)}", -3}, {"fxyz", "f_{xyz}", -2},
         {"fy(5z2-r2)", "f_{y(5z^2-r^2)}", -1},
         {"fz(5z2-3r2)", "f_{z^3}", 0},
         {"fx(5z2-r2)", "f_{x(5z^2-r^2)}", 1},
         {"fz(x2-y2)", "f_{z(x^2-y^2)}", 2},
         {"fx(x2-3y2)", "f_{x(x^2-3y^2)}", 3}},
        {{"gxy(x2-y2)", "g_{xy(x^2-y^2)}", -4},
         {"gyz(3x2-y2)", "g_{yz(3x^2-y^2)}", -3},
         {"gxy(7z2-r2)", "g_{xy(7z^2-r^2)}", -2},
         {"gyz(7z2-3r2)", "g_{yz(7z^2-3r^2)}", -1},
         {"g(35z4-30z2r2+3r4)", "g_{35z^4-30z^2r^2+3r^4}", 0},
         {"gxz(7z2-3r2)", "g_{xz(7z^2-3r^2)}", 1},
         {"g(x2-y2)(7z2-r2)", "g_{(x^2-y^2)(7z^2-r^2)}", 2},
         {"gxz(x2-3y2)", "g_{xz(x^2-3y^2)}", 3},
         {"g(x4-6x2y2+y4)", "g_{x^4-6x^2y^2+y^4}", 4}},
    }};
    return names;
}

void set_error(QString* error_message, const QString& message) {
    if (error_message) {
        *error_message = message;
    }
}

double finite_number(const QJsonObject& object, const QString& key, double fallback) {
    const double value = object.value(key).toDouble(fallback);
    return std::isfinite(value) ? value : fallback;
}

double nearest_order_of_magnitude(double value) {
    if (!std::isfinite(value) || value <= 0.0) return value;
    return std::pow(10.0, std::round(std::log10(value)));
}

bool is_order_of_magnitude(double value) {
    if (!std::isfinite(value) || value <= 0.0) return false;
    const double exponent = std::round(std::log10(value));
    return std::abs(value - std::pow(10.0, exponent))
        <= value * 1.0e-10;
}

double factorial(int value) {
    return std::tgamma(double(value) + 1.0);
}

double associated_laguerre(int degree, int order, double x) {
    if (degree == 0) return 1.0;
    double previous = 1.0;
    double current = double(order + 1) - x;
    for (int index = 2; index <= degree; ++index) {
        const double next = ((double(order + 2 * index - 1) - x) * current
                             - double(order + index - 1) * previous)
            / double(index);
        previous = current;
        current = next;
    }
    return current;
}

double associated_legendre(int degree, int order, double x) {
    x = std::clamp(x, -1.0, 1.0);
    double pmm = 1.0;
    double factor = 1.0;
    const double root = std::sqrt(std::max(0.0, 1.0 - x * x));
    for (int index = 0; index < order; ++index) {
        pmm *= -factor * root;
        factor += 2.0;
    }
    if (degree == order) return pmm;
    double pmmp1 = x * double(2 * order + 1) * pmm;
    if (degree == order + 1) return pmmp1;
    for (int index = order + 2; index <= degree; ++index) {
        const double next = (double(2 * index - 1) * x * pmmp1
                             - double(index + order - 1) * pmm)
            / double(index - order);
        pmm = pmmp1;
        pmmp1 = next;
    }
    return pmmp1;
}

double radial_value(int n, int l, double radius) {
    const double rho = 2.0 * radius / double(n);
    const double normalization = std::sqrt(
        std::pow(2.0 / double(n), 3.0) * factorial(n - l - 1)
        / (2.0 * double(n) * factorial(n + l)));
    return normalization * std::exp(-rho / 2.0) * std::pow(rho, double(l))
        * associated_laguerre(n - l - 1, 2 * l + 1, rho);
}

std::pair<double, double> automatic_extent_and_isovalue(int n, int l) {
    constexpr double maximum_radius = 200.0;
    constexpr int steps = 100000;
    const double step = maximum_radius / double(steps);
    double integral = 0.0;
    double radius = step;
    for (int index = 0; index < steps; ++index) {
        const double r0 = double(index) * step;
        const double r1 = r0 + step;
        const double rm = (r0 + r1) * 0.5;
        const auto density = [n, l](double r) {
            const double value = radial_value(n, l, r);
            return r * r * value * value;
        };
        integral += step * (density(r0) + 4.0 * density(rm) + density(r1)) / 6.0;
        radius = r1;
        if (integral >= kProbabilityCutoff) break;
    }
    const double extent = std::max(2.0, std::ceil(radius * 2.0));
    const double iso = std::abs(radial_value(n, l, radius)) / std::sqrt(4.0 * kPi);
    return {extent, std::max(iso, 1.0e-8)};
}

int volume_index(int size, int x, int y, int z) {
    return (z * size + y) * size + x;
}

QVector3D interpolated_point(
    const QVector3D& first,
    const QVector3D& second,
    float first_value,
    float second_value,
    float isovalue) {
    const float difference = second_value - first_value;
    const float amount = std::abs(difference) > 1.0e-12f
        ? std::clamp((isovalue - first_value) / difference, 0.0f, 1.0f)
        : 0.5f;
    return first + (second - first) * amount;
}

QVector3D orbital_gradient(
    const AtomicOrbitalDefinition& definition,
    const QVector3D& position,
    float spacing) {
    const double delta = std::max(1.0e-4, double(spacing) * 0.35);
    const auto value = [&definition](double x, double y, double z) {
        return hydrogenic_atomic_orbital_value(
            definition.n, definition.l, definition.m, x, y, z);
    };
    return QVector3D(
        float(value(position.x() + delta, position.y(), position.z())
              - value(position.x() - delta, position.y(), position.z())),
        float(value(position.x(), position.y() + delta, position.z())
              - value(position.x(), position.y() - delta, position.z())),
        float(value(position.x(), position.y(), position.z() + delta)
              - value(position.x(), position.y(), position.z() - delta))).normalized();
}

void append_oriented_triangle(
    QVector<AtomicOrbitalVertex>* output,
    QVector3D a,
    QVector3D b,
    QVector3D c,
    float isovalue,
    const AtomicOrbitalDefinition& definition,
    float spacing) {
    QVector3D na = orbital_gradient(definition, a, spacing);
    QVector3D nb = orbital_gradient(definition, b, spacing);
    QVector3D nc = orbital_gradient(definition, c, spacing);
    if (isovalue > 0.0f) {
        na = -na;
        nb = -nb;
        nc = -nc;
    }
    const QVector3D average = na + nb + nc;
    if (QVector3D::dotProduct(QVector3D::crossProduct(b - a, c - a), average) < 0.0f) {
        std::swap(b, c);
        std::swap(nb, nc);
    }
    output->append({{a, na}, {b, nb}, {c, nc}});
}

void polygonise_tetrahedron(
    const std::array<QVector3D, 4>& positions,
    const std::array<float, 4>& values,
    float isovalue,
    const AtomicOrbitalDefinition& definition,
    float spacing,
    QVector<AtomicOrbitalVertex>* output) {
    std::array<int, 4> inside{};
    int inside_count = 0;
    for (int index = 0; index < 4; ++index) {
        if (values[index] >= isovalue) inside[inside_count++] = index;
    }
    if (inside_count == 0 || inside_count == 4) return;

    std::array<int, 4> outside{};
    int outside_count = 0;
    for (int index = 0; index < 4; ++index) {
        bool found = false;
        for (int inside_index = 0; inside_index < inside_count; ++inside_index) {
            found = found || inside[inside_index] == index;
        }
        if (!found) outside[outside_count++] = index;
    }

    const auto point = [&](int first, int second) {
        return interpolated_point(
            positions[first], positions[second], values[first], values[second], isovalue);
    };
    if (inside_count == 1) {
        const QVector3D a = point(inside[0], outside[0]);
        const QVector3D b = point(inside[0], outside[1]);
        const QVector3D c = point(inside[0], outside[2]);
        append_oriented_triangle(output, a, b, c, isovalue, definition, spacing);
    } else if (inside_count == 3) {
        const QVector3D a = point(outside[0], inside[0]);
        const QVector3D b = point(outside[0], inside[1]);
        const QVector3D c = point(outside[0], inside[2]);
        append_oriented_triangle(output, a, c, b, isovalue, definition, spacing);
    } else {
        const QVector3D a = point(inside[0], outside[0]);
        const QVector3D b = point(inside[0], outside[1]);
        const QVector3D c = point(inside[1], outside[0]);
        const QVector3D d = point(inside[1], outside[1]);
        append_oriented_triangle(output, a, b, c, isovalue, definition, spacing);
        append_oriented_triangle(output, b, d, c, isovalue, definition, spacing);
    }
}

QVector<AtomicOrbitalVertex> build_surface(
    const QVector<float>& values,
    int size,
    float half_extent,
    float isovalue,
    const AtomicOrbitalDefinition& definition) {
    QVector<AtomicOrbitalVertex> output;
    const float spacing = 2.0f * half_extent / float(size - 1);
    static constexpr int tetrahedra[6][4] = {
        {0, 2, 3, 7}, {0, 2, 6, 7}, {0, 4, 6, 7},
        {0, 6, 1, 2}, {0, 6, 1, 4}, {5, 6, 1, 4},
    };
    static constexpr int corners[8][3] = {
        {0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0},
        {0, 0, 1}, {0, 1, 1}, {1, 1, 1}, {1, 0, 1},
    };
    for (int z = 0; z < size - 1; ++z) {
        for (int y = 0; y < size - 1; ++y) {
            for (int x = 0; x < size - 1; ++x) {
                std::array<QVector3D, 8> cube_positions;
                std::array<float, 8> cube_values{};
                for (int corner = 0; corner < 8; ++corner) {
                    const int gx = x + corners[corner][0];
                    const int gy = y + corners[corner][1];
                    const int gz = z + corners[corner][2];
                    cube_positions[corner] = QVector3D(
                        -half_extent + float(gx) * spacing,
                        -half_extent + float(gy) * spacing,
                        -half_extent + float(gz) * spacing);
                    cube_values[corner] = values.at(volume_index(size, gx, gy, gz));
                }
                for (const auto& tetrahedron : tetrahedra) {
                    std::array<QVector3D, 4> tetra_positions;
                    std::array<float, 4> tetra_values{};
                    for (int vertex = 0; vertex < 4; ++vertex) {
                        tetra_positions[vertex] = cube_positions[tetrahedron[vertex]];
                        tetra_values[vertex] = cube_values[tetrahedron[vertex]];
                    }
                    polygonise_tetrahedron(
                        tetra_positions, tetra_values, isovalue,
                        definition, spacing, &output);
                }
            }
        }
    }
    return output;
}

struct ColorStop {
    double position;
    QColor color;
};

QVector<ColorStop> color_stops(const QString& requested_name) {
    QString name = requested_name;
    const bool reversed = name.endsWith(QStringLiteral("_r"));
    if (reversed) name.chop(2);
    QVector<ColorStop> stops;
    if (name == QStringLiteral("garnet_teal")) {
      stops = {{0.0, "#177e72"},
               {0.25, "#91c9bf"},
               {0.5, "#fff8fa"},
               {0.75, "#d795aa"},
               {1.0, "#8e1b3e"}};
    } else if (name == QStringLiteral("garnet_slate")) {
      stops = {{0.0, "#426b86"},
               {0.25, "#a5c1d2"},
               {0.5, "#fff8fa"},
               {0.75, "#d795aa"},
               {1.0, "#8e1b3e"}};
    } else if (name == QStringLiteral("seismic")) {
      stops = {{0.0, "#00004c"},
               {0.25, "#0000ff"},
               {0.5, "#ffffff"},
               {0.75, "#ff0000"},
               {1.0, "#4c0000"}};
    } else if (name == QStringLiteral("bwr")) {
      stops = {{0.0, "#0000ff"}, {0.5, "#ffffff"}, {1.0, "#ff0000"}};
    } else if (name == QStringLiteral("RdBu")) {
      stops = {{0.0, "#67001f"},
               {0.25, "#d6604d"},
               {0.5, "#f7f7f7"},
               {0.75, "#4393c3"},
               {1.0, "#053061"}};
    } else if (name == QStringLiteral("PuOr")) {
      stops = {{0.0, "#7f3b08"},
               {0.25, "#fdb863"},
               {0.5, "#f7f7f7"},
               {0.75, "#b2abd2"},
               {1.0, "#2d004b"}};
    } else if (name == QStringLiteral("BrBG")) {
      stops = {{0.0, "#543005"},
               {0.25, "#bf812d"},
               {0.5, "#f5f5f5"},
               {0.75, "#35978f"},
               {1.0, "#003c30"}};
    } else if (name == QStringLiteral("PiYG")) {
      stops = {{0.0, "#8e0152"},
               {0.25, "#de77ae"},
               {0.5, "#f7f7f7"},
               {0.75, "#7fbc41"},
               {1.0, "#276419"}};
    } else if (name == QStringLiteral("PRGn")) {
      stops = {{0.0, "#40004b"},
               {0.25, "#9970ab"},
               {0.5, "#f7f7f7"},
               {0.75, "#5aae61"},
               {1.0, "#00441b"}};
    } else if (name == QStringLiteral("RdGy")) {
      stops = {{0.0, "#67001f"},
               {0.25, "#d6604d"},
               {0.5, "#ffffff"},
               {0.75, "#878787"},
               {1.0, "#1a1a1a"}};
    } else {
      stops = {{0.0, "#3b4cc0"},
               {0.25, "#8db0fe"},
               {0.5, "#dddddd"},
               {0.75, "#f4987a"},
               {1.0, "#b40426"}};
    }
    if (reversed) {
        std::reverse(stops.begin(), stops.end());
        for (ColorStop& stop : stops) stop.position = 1.0 - stop.position;
        std::sort(stops.begin(), stops.end(), [](const ColorStop& a, const ColorStop& b) {
            return a.position < b.position;
        });
    }
    return stops;
}
}  // namespace

bool AtomicOrbitalDefinition::is_valid() const {
    return !title.isEmpty() && !orbital.isEmpty()
        && n >= 1 && n <= 5 && l >= 0 && l < n && std::abs(m) <= l
        && positive_color.isValid() && negative_color.isValid()
        && is_supported_atomic_orbital_colormap(colormap)
        && std::isfinite(isovalue) && isovalue >= 0.0
        && std::isfinite(offset_min) && std::isfinite(offset_max)
        && std::isfinite(offset_initial) && offset_max > offset_min
        && offset_min >= -1.0 && offset_max <= 1.0
        && offset_initial >= offset_min && offset_initial <= offset_max
        && contour_maximum > kAtomicOrbitalContourMinimum
        && contour_maximum <= 1.0
        && is_order_of_magnitude(contour_maximum)
        && contour_levels >= 2 && contour_levels <= 32
        && grid_size >= 33 && grid_size <= 129 && grid_size % 2 == 1;
}

bool AtomicOrbitalVolume::is_valid() const {
    return grid_size >= 2
        && values.size() == grid_size * grid_size * grid_size
        && (!positive_vertices.isEmpty() || !negative_vertices.isEmpty())
        && half_extent > 0.0f && maximum_absolute_value > 0.0f && isovalue > 0.0f;
}

QVector<AtomicOrbitalCatalogEntry> atomic_orbital_catalog() {
    QVector<AtomicOrbitalCatalogEntry> catalog;
    catalog.reserve(55);
    for (int n = 1; n <= 5; ++n) {
        for (int l = 0; l < n; ++l) {
            for (const AngularName& angular : angular_names().at(l)) {
                catalog.push_back({
                    QString::number(n) + QString::fromLatin1(angular.suffix),
                    QString::number(n) + QString::fromLatin1(angular.label),
                    n, l, angular.m});
            }
        }
    }
    return catalog;
}

bool resolve_atomic_orbital(const QString& name, AtomicOrbitalCatalogEntry* entry) {
    const QString normalized = name.simplified().remove(QLatin1Char(' ')).toLower();
    for (const AtomicOrbitalCatalogEntry& candidate : atomic_orbital_catalog()) {
        if (candidate.name.toLower() == normalized) {
            if (entry) *entry = candidate;
            return true;
        }
    }
    return false;
}

double hydrogenic_atomic_orbital_value(
    int n, int l, int m, double x, double y, double z) {
    if (n < 1 || n > 5 || l < 0 || l >= n || std::abs(m) > l) return 0.0;
    const double radius = std::sqrt(x * x + y * y + z * z);
    const double cos_theta = radius > 1.0e-14 ? z / radius : 1.0;
    const double phi = std::atan2(y, x);
    const int absolute_m = std::abs(m);
    const double polar_normalization = std::sqrt(
        double(2 * l + 1) * factorial(l - absolute_m) / factorial(l + absolute_m));
    const double polar = polar_normalization * associated_legendre(l, absolute_m, cos_theta);
    const double azimuthal_normalization = 1.0 / std::sqrt(4.0 * kPi);
    double azimuthal = azimuthal_normalization;
    if (m > 0) azimuthal *= std::sqrt(2.0) * std::cos(double(m) * phi);
    if (m < 0) azimuthal *= std::sqrt(2.0) * std::sin(double(-m) * phi);
    return radial_value(n, l, radius) * polar * azimuthal;
}

bool parse_atomic_orbital(
    const QByteArray& payload,
    AtomicOrbitalDefinition* definition,
    QString* error_message) {
    if (!definition) {
        set_error(error_message, QStringLiteral("Missing atomic-orbital output"));
        return false;
    }
    if (payload.isEmpty() || payload.size() > kMaximumPayloadBytes) {
        set_error(error_message, QStringLiteral("Atomic-orbital payload is empty or exceeds 1 MiB"));
        return false;
    }
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        set_error(error_message,
                  QStringLiteral("Atomic-orbital JSON is invalid: %1")
                      .arg(parse_error.errorString()));
        return false;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString()
            != QStringLiteral("uil.atomic-orbital")
        || root.value(QStringLiteral("version")).toInt(-1) != 1) {
        set_error(error_message, QStringLiteral("Unsupported atomic-orbital format or version"));
        return false;
    }

    AtomicOrbitalDefinition parsed;
    parsed.title = root.value(QStringLiteral("title")).toString(parsed.title);
    parsed.orbital = root.value(QStringLiteral("orbital")).toString();
    AtomicOrbitalCatalogEntry orbital;
    if (!resolve_atomic_orbital(parsed.orbital, &orbital)) {
        set_error(error_message, QStringLiteral("Unsupported atomic orbital: %1").arg(parsed.orbital));
        return false;
    }
    parsed.orbital = orbital.name;
    parsed.n = orbital.n;
    parsed.l = orbital.l;
    parsed.m = orbital.m;

    const QJsonObject surface = root.value(QStringLiteral("surface")).toObject();
    parsed.isovalue = finite_number(surface, QStringLiteral("isovalue"), parsed.isovalue);
    const QColor positive(surface.value(QStringLiteral("positive_color")).toString());
    const QColor negative(surface.value(QStringLiteral("negative_color")).toString());
    if (positive.isValid()) parsed.positive_color = positive;
    if (negative.isValid()) parsed.negative_color = negative;
    parsed.grid_size = surface.value(QStringLiteral("grid_size")).toInt(parsed.grid_size);

    const QJsonObject sampling = root.value(QStringLiteral("sampling_plane")).toObject();
    const QString plane = sampling.value(QStringLiteral("type")).toString(QStringLiteral("xy"));
    if (plane == QStringLiteral("xy")) parsed.plane = AtomicOrbitalDefinition::Plane::XY;
    else if (plane == QStringLiteral("xz")) parsed.plane = AtomicOrbitalDefinition::Plane::XZ;
    else if (plane == QStringLiteral("yz")) parsed.plane = AtomicOrbitalDefinition::Plane::YZ;
    else {
        set_error(error_message, QStringLiteral("Sampling plane must be xy, xz, or yz"));
        return false;
    }
    const QJsonObject offset = sampling.value(QStringLiteral("offset")).toObject();
    parsed.offset_min = finite_number(offset, QStringLiteral("min"), parsed.offset_min);
    parsed.offset_max = finite_number(offset, QStringLiteral("max"), parsed.offset_max);
    parsed.offset_initial = finite_number(offset, QStringLiteral("value"), parsed.offset_initial);

    const QJsonObject contour = root.value(QStringLiteral("contour")).toObject();
    parsed.colormap = contour.value(QStringLiteral("colormap")).toString(parsed.colormap);
    parsed.contour_maximum = nearest_order_of_magnitude(finite_number(
        contour, QStringLiteral("maximum"), parsed.contour_maximum));
    parsed.contour_levels = contour.value(QStringLiteral("levels")).toInt(parsed.contour_levels);

    if (parsed.title.size() > 200 || !parsed.is_valid()) {
        set_error(error_message, QStringLiteral("Atomic-orbital values or ranges are invalid"));
        return false;
    }
    *definition = std::move(parsed);
    if (error_message) error_message->clear();
    return true;
}

AtomicOrbitalVolume build_atomic_orbital_volume(
    const AtomicOrbitalDefinition& definition,
    QString* error_message) {
    AtomicOrbitalVolume volume;
    if (!definition.is_valid()) {
        set_error(error_message, QStringLiteral("Cannot build an invalid atomic-orbital definition"));
        return volume;
    }
    const auto automatic = automatic_extent_and_isovalue(definition.n, definition.l);
    volume.grid_size = definition.grid_size;
    volume.half_extent = float(automatic.first);
    volume.isovalue = float(definition.isovalue > 0.0 ? definition.isovalue : automatic.second);
    const int size = volume.grid_size;
    const float spacing = 2.0f * volume.half_extent / float(size - 1);
    volume.values.resize(size * size * size);
    for (int z = 0; z < size; ++z) {
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const double px = -volume.half_extent + float(x) * spacing;
                const double py = -volume.half_extent + float(y) * spacing;
                const double pz = -volume.half_extent + float(z) * spacing;
                const float value = float(hydrogenic_atomic_orbital_value(
                    definition.n, definition.l, definition.m, px, py, pz));
                volume.values[volume_index(size, x, y, z)] = std::isfinite(value) ? value : 0.0f;
                volume.maximum_absolute_value = std::max(
                    volume.maximum_absolute_value, std::abs(value));
            }
        }
    }
    if (!(volume.maximum_absolute_value > volume.isovalue)) {
        set_error(error_message, QStringLiteral("Atomic-orbital isovalue does not cross the sampled field"));
        return {};
    }
    volume.positive_vertices = build_surface(
        volume.values, size, volume.half_extent, volume.isovalue, definition);
    volume.negative_vertices = build_surface(
        volume.values, size, volume.half_extent, -volume.isovalue, definition);
    if (!volume.is_valid()) {
        set_error(error_message, QStringLiteral("Could not construct an orbital phase surface"));
        return {};
    }
    if (error_message) error_message->clear();
    return volume;
}

bool is_supported_atomic_orbital_colormap(const QString& requested_name) {
    QString name = requested_name;
    if (name.endsWith(QStringLiteral("_r"))) name.chop(2);
    static const std::array<const char*, 11> supported{{
        "coolwarm",
        "seismic",
        "bwr",
        "RdBu",
        "PuOr",
        "BrBG",
        "PiYG",
        "PRGn",
        "RdGy",
        "garnet_teal",
        "garnet_slate",
    }};
    return std::any_of(supported.begin(), supported.end(), [&name](const char* candidate) {
        return name == QString::fromLatin1(candidate);
    });
}

QVector<QColor> atomic_orbital_colormap(const QString& name) {
    if (!is_supported_atomic_orbital_colormap(name)) return {};
    const QVector<ColorStop> stops = color_stops(name);
    QVector<QColor> colors;
    colors.reserve(256);
    for (int index = 0; index < 256; ++index) {
        const double position = double(index) / 255.0;
        int upper = 1;
        while (upper < stops.size() && stops.at(upper).position < position) ++upper;
        upper = std::clamp(upper, 1, int(stops.size()) - 1);
        const ColorStop& first = stops.at(upper - 1);
        const ColorStop& second = stops.at(upper);
        const double amount = std::clamp(
            (position - first.position) / (second.position - first.position), 0.0, 1.0);
        colors.push_back(QColor::fromRgbF(
            first.color.redF() + (second.color.redF() - first.color.redF()) * amount,
            first.color.greenF() + (second.color.greenF() - first.color.greenF()) * amount,
            first.color.blueF() + (second.color.blueF() - first.color.blueF()) * amount));
    }
    return colors;
}
