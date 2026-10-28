#include "rechigros/game/RpeTrajectory.hpp"

#include "rechigros/game/Easing.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <stdexcept>
#include <utility>

namespace rechigros::game {
namespace {

constexpr double PI = 3.14159265358979323846;
using Variables = RpeTrajectory::Variables;
using Expression = std::function<double(const Variables&)>;

std::string normalize(std::string source) {
  const auto replaceAll = [&source](const std::string& from, const std::string& to) {
    for (std::size_t position = 0; (position = source.find(from, position)) != std::string::npos;) {
      source.replace(position, from.size(), to);
      position += to.size();
    }
  };
  replaceAll("$t$", "t");
  replaceAll("θ", "theta");
  replaceAll("Pi", "pi");
  replaceAll("π", "pi");
  replaceAll("，", ",");
  replaceAll("（", "(");
  replaceAll("）", ")");
  source.erase(std::remove_if(source.begin(), source.end(), [](unsigned char c) {
    return std::isspace(c) != 0;
  }), source.end());
  return source;
}

class ExpressionParser final {
public:
  explicit ExpressionParser(std::string source) : source_(normalize(source)) {
    if (source_.empty()) source_ = "0";
    if (source_.size() > 4096) throw std::invalid_argument("expression is too long");
    tokenize();
    if (tokens_.size() > 512) throw std::invalid_argument("expression has too many terms");
    result_ = expression();
    if (cursor_ != tokens_.size()) throw std::invalid_argument("unexpected expression term");
  }

  double evaluate(const Variables& variables) const {
    const double result = result_(variables);
    if (!std::isfinite(result)) throw std::runtime_error("expression is not finite");
    return result;
  }

private:
  void tokenize() {
    std::size_t position = 0;
    while (position < source_.size()) {
      const char c = source_[position];
      if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
        char* end = nullptr;
        const double value = std::strtod(source_.c_str() + position, &end);
        if (end == source_.c_str() + position) throw std::invalid_argument("invalid number");
        tokens_.push_back(source_.substr(position,
            static_cast<std::size_t>(end - source_.c_str() - position)));
        position = static_cast<std::size_t>(end - source_.c_str());
        (void)value;
      } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        const std::size_t start = position++;
        while (position < source_.size() &&
            (std::isalnum(static_cast<unsigned char>(source_[position])) || source_[position] == '_' ||
             source_[position] == '.')) ++position;
        tokens_.push_back(source_.substr(start, position - start));
      } else {
        const std::string pair = position + 1 < source_.size() ? source_.substr(position, 2) : "";
        if (pair == "**" || pair == "&&" || pair == "||" || pair == "==" || pair == "!=" ||
            pair == "<=" || pair == ">=") {
          tokens_.push_back(pair);
          position += 2;
        } else if (std::string("+-*/%^<>()!,?:=").find(c) != std::string::npos) {
          tokens_.emplace_back(1, c);
          ++position;
        } else {
          throw std::invalid_argument("invalid expression character");
        }
      }
    }
  }

  std::string take() {
    if (cursor_ >= tokens_.size()) throw std::invalid_argument("incomplete expression");
    return tokens_[cursor_++];
  }

  void require(const std::string& token) {
    if (take() != token) throw std::invalid_argument("missing expression delimiter");
  }

  Expression atom() {
    const std::string token = take();
    if (token == "+" || token == "-" || token == "!") {
      auto value = atom();
      return [token, value = std::move(value)](const Variables& variables) {
        const double result = value(variables);
        return token == "-" ? -result : token == "!" ? (result == 0.0 ? 1.0 : 0.0) : result;
      };
    }
    if (token == "(") {
      auto value = expression();
      require(")");
      return value;
    }
    if (std::isdigit(static_cast<unsigned char>(token.front())) || token.front() == '.') {
      const double number = std::stod(token);
      return [number](const Variables&) { return number; };
    }
    if (!std::isalpha(static_cast<unsigned char>(token.front())) && token.front() != '_') {
      throw std::invalid_argument("invalid expression atom");
    }
    if (cursor_ < tokens_.size() && tokens_[cursor_] == "(") {
      ++cursor_;
      std::vector<Expression> arguments;
      if (cursor_ < tokens_.size() && tokens_[cursor_] != ")") {
        for (;;) {
          arguments.push_back(expression());
          if (cursor_ >= tokens_.size() || tokens_[cursor_] != ",") break;
          ++cursor_;
        }
      }
      require(")");
      return [token, arguments = std::move(arguments)](const Variables& variables) {
        const auto argument = [&variables, &arguments](std::size_t index) {
          if (index >= arguments.size()) throw std::invalid_argument("wrong function arity");
          return arguments[index](variables);
        };
        const auto optional = [&argument, &arguments](std::size_t index, double fallback) {
          return index < arguments.size() ? argument(index) : fallback;
        };
        if (token == "abs") return std::abs(argument(0));
        if (token == "min") return std::min(argument(0), argument(1));
        if (token == "max") return std::max(argument(0), argument(1));
        if (token == "floor") return std::floor(argument(0));
        if (token == "ceil") return std::ceil(argument(0));
        if (token == "round") return std::floor(argument(0) + 0.5);
        if (token == "sin") return std::sin(argument(0));
        if (token == "cos") return std::cos(argument(0));
        if (token == "tan") return std::tan(argument(0));
        if (token == "sqrt") return std::sqrt(argument(0));
        if (token == "pow") return std::pow(argument(0), argument(1));
        if (token == "clamp") return std::clamp(argument(0), argument(1), argument(2));
        if (token == "lerp") return argument(0) + (argument(1) - argument(0)) * argument(2);
        if (token == "ease") return Easing::apply(static_cast<int>(optional(1, 1.0)) - 1, argument(0));
        throw std::invalid_argument("unknown expression function");
      };
    }
    return [token](const Variables& variables) {
      if (token == "pi") return PI;
      if (token == "true") return 1.0;
      if (token == "false") return 0.0;
      const auto found = variables.find(token);
      if (found == variables.end()) throw std::invalid_argument("unknown expression variable");
      return found->second;
    };
  }

  static int precedence(const std::string& token) {
    if (token == "||") return 1;
    if (token == "&&") return 2;
    if (token == "==" || token == "!=") return 3;
    if (token == "<" || token == ">" || token == "<=" || token == ">=") return 4;
    if (token == "+" || token == "-") return 5;
    if (token == "*" || token == "/" || token == "%") return 6;
    if (token == "^" || token == "**") return 7;
    return 0;
  }

  Expression expression(int minimum = 0) {
    auto left = atom();
    while (cursor_ < tokens_.size()) {
      const int operatorPrecedence = precedence(tokens_[cursor_]);
      if (operatorPrecedence == 0 || operatorPrecedence < minimum) break;
      const std::string op = take();
      const bool rightAssociative = op == "^" || op == "**";
      auto right = expression(operatorPrecedence + (rightAssociative ? 0 : 1));
      left = [op, left = std::move(left), right = std::move(right)](const Variables& variables) {
        const double first = left(variables);
        if (op == "&&") return first != 0.0 && right(variables) != 0.0 ? 1.0 : 0.0;
        if (op == "||") return first != 0.0 || right(variables) != 0.0 ? 1.0 : 0.0;
        const double second = right(variables);
        if (op == "+") return first + second;
        if (op == "-") return first - second;
        if (op == "*") return first * second;
        if (op == "/") return first / second;
        if (op == "%") return std::fmod(first, second);
        if (op == "^" || op == "**") return std::pow(first, second);
        if (op == "==") return first == second ? 1.0 : 0.0;
        if (op == "!=") return first != second ? 1.0 : 0.0;
        if (op == "<") return first < second ? 1.0 : 0.0;
        if (op == ">") return first > second ? 1.0 : 0.0;
        if (op == "<=") return first <= second ? 1.0 : 0.0;
        if (op == ">=") return first >= second ? 1.0 : 0.0;
        throw std::invalid_argument("invalid operator");
      };
    }
    if (minimum == 0 && cursor_ < tokens_.size() && tokens_[cursor_] == "?") {
      ++cursor_;
      auto yes = expression();
      require(":");
      auto no = expression();
      left = [left = std::move(left), yes = std::move(yes), no = std::move(no)](const Variables& variables) {
        return left(variables) != 0.0 ? yes(variables) : no(variables);
      };
    }
    return left;
  }

  std::string source_;
  std::vector<std::string> tokens_;
  std::size_t cursor_{};
  Expression result_;
};

struct CompiledExpression {
  ExpressionParser parser;
  explicit CompiledExpression(const std::string& source) : parser(source) {}
  double operator()(const Variables& variables) const { return parser.evaluate(variables); }
};

} // namespace

struct RpeTrajectory::Impl final {
  explicit Impl(const RpeTrajectoryOptions& input) : options(input) {
    if (options.trimStart < 0.0 || options.trimEnd > 1.0 || options.trimEnd <= options.trimStart ||
        options.easingX < 1 || options.easingX > 29 || options.easingY < 1 || options.easingY > 29 ||
        options.parameters.size() > 32) throw std::invalid_argument("invalid trajectory options");
    for (const auto& [key, value] : options.parameters) {
      if (key.empty() || (!std::isalpha(static_cast<unsigned char>(key.front())) && key.front() != '_') ||
          !std::all_of(key.begin(), key.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '_';
          }) || !std::isfinite(value)) throw std::invalid_argument("invalid trajectory parameter");
    }
    variables = options.parameters;
    variables["seed"] = options.seed;
    variables["randomness"] = options.randomness;
    polar = options.mode == "polar";
    begin = CompiledExpression(polar ? options.angleStart : options.parameterStart);
    end = CompiledExpression(polar ? options.angleEnd : options.parameterEnd);
    if (!polar && options.shape.empty()) {
      horizontal = CompiledExpression(options.xExpression);
      vertical = CompiledExpression(options.yExpression);
    } else if (polar) {
      radial = CompiledExpression(options.radiusExpression);
    }
    if (!options.tangentRotation && !options.rotationExpression.empty()) {
      direction = CompiledExpression(options.rotationExpression);
    }
    const double firstBegin = begin(variables);
    const double lastEnd = end(variables);
    (void)firstBegin;
    (void)lastEnd;
    first = raw(0.0);
    if (options.tangentRotation) {
      angles.reserve(1025);
      for (int index = 0; index <= 1024; ++index) {
        const double previous = angles.empty() ? 0.0 : angles.back();
        const double angle = tangent(static_cast<double>(index) / 1024.0).value_or(previous);
        angles.push_back(index == 0 ? angle : angle + 360.0 * std::round((previous - angle) / 360.0));
      }
    }
  }

  double coordinate(double unit, bool x) const {
    return begin(variables) + (end(variables) - begin(variables)) *
        Easing::apply((x ? options.easingX : options.easingY) - 1, Easing::clamp01(unit));
  }

  RpeTrajectorySample raw(double progress) const {
    const double unit = options.trimStart + (options.trimEnd - options.trimStart) * progress;
    double x = 0.0;
    double y = 0.0;
    if (!options.shape.empty()) {
      const double pointCount = std::clamp(std::round(parameter("points", 5.0)), 3.0, 40.0) *
          (options.shape == "star" ? 2.0 : 1.0);
      const double amount = std::fmod(std::fmod(unit, 1.0) + 1.0, 1.0) * pointCount;
      const int index = static_cast<int>(std::floor(amount));
      const double fraction = amount - index;
      const auto vertex = [&](int step) {
        const double wrapped = std::fmod(step, pointCount);
        const double noise = std::sin(wrapped * 127.1 + options.seed * 311.7) * 43758.5453;
        const double radius = parameter("radius", 200.0) *
            (options.shape == "star" ? (std::fmod(wrapped, 2.0) != 0.0 ? parameter("inner", 0.4) : 1.0)
                                     : 1.0 - options.randomness * (noise - std::floor(noise)));
        const double angle = 2.0 * PI * wrapped / pointCount;
        return std::pair<double, double>{radius * std::cos(angle), radius * std::sin(angle)};
      };
      const auto a = vertex(index);
      const auto b = vertex(index + 1);
      x = a.first + (b.first - a.first) * fraction;
      y = a.second + (b.second - a.second) * fraction;
    } else if (polar) {
      const double theta = coordinate(unit, true);
      auto scope = variables;
      scope["theta"] = theta;
      scope["t"] = unit;
      scope["u"] = progress;
      const double radius = (*radial)(scope);
      x = radius * std::cos(theta);
      y = radius * std::sin(theta);
    } else {
      auto horizontalScope = variables;
      horizontalScope["t"] = coordinate(unit, true);
      horizontalScope["u"] = progress;
      auto verticalScope = variables;
      verticalScope["t"] = coordinate(unit, false);
      verticalScope["u"] = progress;
      x = (*horizontal)(horizontalScope);
      y = (*vertical)(verticalScope);
    }
    const double angle = options.rotation * PI / 180.0;
    const double sx = x * options.scaleX;
    const double sy = y * options.scaleY;
    RpeTrajectorySample result{sx * std::cos(angle) - sy * std::sin(angle),
        sx * std::sin(angle) + sy * std::cos(angle), 0.0};
    if (direction) {
      auto scope = variables;
      scope["t"] = polar ? unit : coordinate(unit, true);
      scope["theta"] = coordinate(unit, true);
      scope["u"] = progress;
      result.rotation = (*direction)(scope);
    }
    return result;
  }

  std::optional<double> tangent(double progress) const {
    for (const double delta : {0.00001, 0.0001, 0.001, 0.01}) {
      const auto before = raw(std::max(0.0, progress - delta));
      const auto after = raw(std::min(1.0, progress + delta));
      const double dx = after.x - before.x;
      const double dy = after.y - before.y;
      if (std::hypot(dx, dy) > 1e-9) return -std::atan2(dy, dx) * 180.0 / PI;
    }
    return std::nullopt;
  }

  double parameter(const std::string& name, double fallback) const {
    const auto found = options.parameters.find(name);
    return found == options.parameters.end() ? fallback : found->second;
  }

  RpeTrajectoryOptions options;
  Variables variables;
  bool polar{};
  CompiledExpression begin{"0"};
  CompiledExpression end{"1"};
  std::optional<CompiledExpression> horizontal, vertical, radial, direction;
  RpeTrajectorySample first;
  std::vector<double> angles;
};

RpeTrajectory::RpeTrajectory(const RpeTrajectoryOptions& options)
    : impl_(std::make_shared<Impl>(options)) {}

RpeTrajectorySample RpeTrajectory::sample(double progress) const noexcept {
  try {
    progress = std::clamp(progress, 0.0, 1.0);
    auto point = impl_->raw(progress);
    if (impl_->options.tangentRotation) {
      const int index = static_cast<int>(std::lround(progress * 1024.0));
      const double reference = impl_->angles[static_cast<std::size_t>(index)];
      const double angle = impl_->tangent(progress).value_or(reference);
      point.rotation = angle + 360.0 * std::round((reference - angle) / 360.0);
    }
    point.x += impl_->options.alignStart ? impl_->options.startX - impl_->first.x : 0.0;
    point.y += impl_->options.alignStart ? impl_->options.startY - impl_->first.y : 0.0;
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.rotation)) {
      return {};
    }
    return point;
  } catch (...) {
    return {};
  }
}

bool RpeTrajectory::hasRotation() const noexcept {
  return impl_->options.tangentRotation || !impl_->options.rotationExpression.empty();
}

} // namespace rechigros::game
