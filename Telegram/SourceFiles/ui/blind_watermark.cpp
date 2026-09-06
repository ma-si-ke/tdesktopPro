/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/blind_watermark.h"

#include <QtGui/QImage>

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <random>

namespace Ui::BlindWatermark {
namespace {

constexpr auto kBlock = 4;
constexpr auto kBlockCells = kBlock * kBlock;
constexpr auto kD1 = 36.;
constexpr auto kD2 = 20.;
constexpr auto kPi = 3.14159265358979323846;

using Matrix = std::array<std::array<double, kBlock>, kBlock>;
using Vector = std::array<double, kBlock>;
using Permutation = std::array<int, kBlockCells>;

// Reproduces the parts of numpy.random.RandomState (legacy MT19937
// interface) that the Python library relies on, so the permutations
// and the watermark shuffle match bit for bit.
class NumpyRandom final {
public:
	explicit NumpyRandom(uint32 seed) : _engine(seed) {
	}

	// RandomState.random(): 53-bit double from two 32-bit draws.
	[[nodiscard]] double random() {
		const auto a = next() >> 5;
		const auto b = next() >> 6;
		return (a * 67108864. + b) / 9007199254740992.;
	}

	// RandomState.shuffle(): Fisher-Yates with masked rejection sampling.
	template <typename T>
	void shuffle(std::vector<T> &list) {
		if (list.size() < 2) {
			return;
		}
		for (auto i = list.size() - 1; i > 0; --i) {
			const auto j = interval(uint32(i));
			std::swap(list[i], list[j]);
		}
	}

private:
	[[nodiscard]] uint32 next() {
		return uint32(_engine());
	}
	[[nodiscard]] uint32 interval(uint32 max) {
		if (!max) {
			return 0;
		}
		auto mask = max;
		mask |= mask >> 1;
		mask |= mask >> 2;
		mask |= mask >> 4;
		mask |= mask >> 8;
		mask |= mask >> 16;
		while (true) {
			const auto value = next() & mask;
			if (value <= max) {
				return value;
			}
		}
	}

	std::mt19937 _engine;

};

[[nodiscard]] Matrix Identity() {
	auto result = Matrix();
	for (auto i = 0; i != kBlock; ++i) {
		for (auto j = 0; j != kBlock; ++j) {
			result[i][j] = (i == j) ? 1. : 0.;
		}
	}
	return result;
}

[[nodiscard]] Matrix Transpose(const Matrix &a) {
	auto result = Matrix();
	for (auto i = 0; i != kBlock; ++i) {
		for (auto j = 0; j != kBlock; ++j) {
			result[i][j] = a[j][i];
		}
	}
	return result;
}

[[nodiscard]] Matrix Multiply(const Matrix &a, const Matrix &b) {
	auto result = Matrix();
	for (auto i = 0; i != kBlock; ++i) {
		for (auto j = 0; j != kBlock; ++j) {
			auto sum = 0.;
			for (auto k = 0; k != kBlock; ++k) {
				sum += a[i][k] * b[k][j];
			}
			result[i][j] = sum;
		}
	}
	return result;
}

// Orthonormal DCT-II basis, the same scaling as cv2.dct.
[[nodiscard]] const Matrix &DctBasis() {
	static const auto result = [] {
		auto basis = Matrix();
		for (auto u = 0; u != kBlock; ++u) {
			const auto scale = (u == 0)
				? std::sqrt(1. / kBlock)
				: std::sqrt(2. / kBlock);
			for (auto i = 0; i != kBlock; ++i) {
				basis[u][i] = scale
					* std::cos((2 * i + 1) * u * kPi / (2 * kBlock));
			}
		}
		return basis;
	}();
	return result;
}

[[nodiscard]] Matrix Dct(const Matrix &block) {
	const auto &c = DctBasis();
	return Multiply(Multiply(c, block), Transpose(c));
}

[[nodiscard]] Matrix InverseDct(const Matrix &block) {
	const auto &c = DctBasis();
	return Multiply(Multiply(Transpose(c), block), c);
}

struct Svd {
	Matrix u;
	Vector s; // Descending, like numpy.linalg.svd.
	Matrix v; // A = u * diag(s) * transpose(v).
};

// Cyclic Jacobi eigen-decomposition of transpose(A) * A gives V and the
// squared singular values; U follows from A * V. Columns of U belonging
// to zero singular values are completed to an orthonormal basis, which
// is what any SVD routine does for rank-deficient blocks (a flat colour
// region gives a rank-1 DCT block).
[[nodiscard]] Svd ComputeSvd(const Matrix &a) {
	auto s = Multiply(Transpose(a), a);
	auto v = Identity();
	for (auto sweep = 0; sweep != 30; ++sweep) {
		auto off = 0.;
		for (auto p = 0; p != kBlock; ++p) {
			for (auto q = p + 1; q != kBlock; ++q) {
				off += s[p][q] * s[p][q];
			}
		}
		if (off < 1e-22) {
			break;
		}
		for (auto p = 0; p != kBlock; ++p) {
			for (auto q = p + 1; q != kBlock; ++q) {
				if (std::abs(s[p][q]) < 1e-30) {
					continue;
				}
				const auto theta = (s[q][q] - s[p][p]) / (2. * s[p][q]);
				const auto sign = (theta >= 0.) ? 1. : -1.;
				const auto t = sign
					/ (std::abs(theta) + std::sqrt(theta * theta + 1.));
				const auto c = 1. / std::sqrt(t * t + 1.);
				const auto sn = t * c;
				auto rotation = Identity();
				rotation[p][p] = c;
				rotation[q][q] = c;
				rotation[p][q] = sn;
				rotation[q][p] = -sn;
				s = Multiply(Multiply(Transpose(rotation), s), rotation);
				v = Multiply(v, rotation);
			}
		}
	}

	auto order = std::array<int, kBlock>();
	std::iota(order.begin(), order.end(), 0);
	std::sort(order.begin(), order.end(), [&](int l, int r) {
		return s[l][l] > s[r][r];
	});

	auto result = Svd();
	for (auto k = 0; k != kBlock; ++k) {
		const auto from = order[k];
		result.s[k] = std::sqrt(std::max(s[from][from], 0.));
		for (auto i = 0; i != kBlock; ++i) {
			result.v[i][k] = v[i][from];
		}
	}
	for (auto k = 0; k != kBlock; ++k) {
		auto column = Vector();
		if (result.s[k] > 1e-6) {
			for (auto i = 0; i != kBlock; ++i) {
				auto sum = 0.;
				for (auto j = 0; j != kBlock; ++j) {
					sum += a[i][j] * result.v[j][k];
				}
				column[i] = sum / result.s[k];
			}
		} else {
			// Gram-Schmidt against the columns found so far.
			for (auto e = 0; e != kBlock; ++e) {
				auto candidate = Vector();
				candidate[e] = 1.;
				for (auto j = 0; j != k; ++j) {
					auto dot = 0.;
					for (auto i = 0; i != kBlock; ++i) {
						dot += result.u[i][j] * candidate[i];
					}
					for (auto i = 0; i != kBlock; ++i) {
						candidate[i] -= dot * result.u[i][j];
					}
				}
				auto norm = 0.;
				for (auto i = 0; i != kBlock; ++i) {
					norm += candidate[i] * candidate[i];
				}
				norm = std::sqrt(norm);
				if (norm > 1e-3) {
					for (auto i = 0; i != kBlock; ++i) {
						column[i] = candidate[i] / norm;
					}
					break;
				}
			}
		}
		for (auto i = 0; i != kBlock; ++i) {
			result.u[i][k] = column[i];
		}
	}
	return result;
}

[[nodiscard]] Matrix Reconstruct(const Svd &svd) {
	auto result = Matrix();
	for (auto i = 0; i != kBlock; ++i) {
		for (auto j = 0; j != kBlock; ++j) {
			auto sum = 0.;
			for (auto k = 0; k != kBlock; ++k) {
				sum += svd.u[i][k] * svd.s[k] * svd.v[j][k];
			}
			result[i][j] = sum;
		}
	}
	return result;
}

[[nodiscard]] double Quantize(double value, double step, uchar bit) {
	return (std::floor(value / step) + 0.25 + 0.5 * bit) * step;
}

// dct -> shuffle -> svd -> quantize -> unshuffle -> idct, exactly the
// library's block_add_wm_slow().
[[nodiscard]] Matrix EmbedBlock(
		const Matrix &block,
		const Permutation &permutation,
		uchar bit) {
	const auto transformed = Dct(block);
	auto flat = std::array<double, kBlockCells>();
	for (auto i = 0; i != kBlock; ++i) {
		for (auto j = 0; j != kBlock; ++j) {
			flat[i * kBlock + j] = transformed[i][j];
		}
	}
	auto shuffled = Matrix();
	for (auto k = 0; k != kBlockCells; ++k) {
		shuffled[k / kBlock][k % kBlock] = flat[permutation[k]];
	}

	auto svd = ComputeSvd(shuffled);
	svd.s[0] = Quantize(svd.s[0], kD1, bit);
	svd.s[1] = Quantize(svd.s[1], kD2, bit);
	const auto rebuilt = Reconstruct(svd);

	auto restored = std::array<double, kBlockCells>();
	for (auto k = 0; k != kBlockCells; ++k) {
		restored[permutation[k]] = rebuilt[k / kBlock][k % kBlock];
	}
	auto result = Matrix();
	for (auto i = 0; i != kBlock; ++i) {
		for (auto j = 0; j != kBlock; ++j) {
			result[i][j] = restored[i * kBlock + j];
		}
	}
	return InverseDct(result);
}

[[nodiscard]] std::vector<Permutation> MakePermutations(
		uint32 seed,
		int count) {
	auto random = NumpyRandom(seed);
	auto result = std::vector<Permutation>(count);
	auto values = std::array<double, kBlockCells>();
	for (auto &permutation : result) {
		for (auto &value : values) {
			value = random.random();
		}
		std::iota(permutation.begin(), permutation.end(), 0);
		std::sort(permutation.begin(), permutation.end(), [&](int l, int r) {
			return values[l] < values[r];
		});
	}
	return result;
}

} // namespace

std::vector<uchar> BytesToBits(const QByteArray &bytes) {
	auto result = std::vector<uchar>();
	result.reserve(bytes.size() * 8);
	for (const auto byte : bytes) {
		for (auto k = 7; k >= 0; --k) {
			result.push_back((uchar(byte) >> k) & 1);
		}
	}
	return result;
}

bool Embed(
		QImage &image,
		const std::vector<uchar> &bits,
		uint32 passwordImage,
		uint32 passwordWatermark) {
	if (image.isNull() || bits.empty()) {
		return false;
	}
	const auto ratio = image.devicePixelRatio();
	auto rgb = image.convertToFormat(QImage::Format_RGB32);
	const auto width = rgb.width();
	const auto height = rgb.height();
	const auto paddedWidth = width + (width % 2);
	const auto paddedHeight = height + (height % 2);
	const auto lowWidth = paddedWidth / 2;
	const auto lowHeight = paddedHeight / 2;
	const auto blocksX = lowWidth / kBlock;
	const auto blocksY = lowHeight / kBlock;
	const auto blockCount = blocksX * blocksY;
	if (blockCount <= int(bits.size())) {
		return false;
	}

	auto watermark = bits;
	NumpyRandom(passwordWatermark).shuffle(watermark);
	const auto permutations = MakePermutations(passwordImage, blockCount);

	// BGR -> YUV the way cv2.cvtColor does it for float images.
	auto planes = std::array<std::vector<float>, 3>();
	for (auto &plane : planes) {
		plane.assign(paddedWidth * paddedHeight, 0.f);
	}
	for (auto y = 0; y != height; ++y) {
		const auto line = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
		for (auto x = 0; x != width; ++x) {
			const auto r = float(qRed(line[x]));
			const auto g = float(qGreen(line[x]));
			const auto b = float(qBlue(line[x]));
			const auto luma = 0.299f * r + 0.587f * g + 0.114f * b;
			const auto index = y * paddedWidth + x;
			planes[0][index] = luma;
			planes[1][index] = (b - luma) * 0.492f + 0.5f;
			planes[2][index] = (r - luma) * 0.877f + 0.5f;
		}
	}

	auto low = std::vector<float>(lowWidth * lowHeight);
	auto horizontal = std::vector<float>(lowWidth * lowHeight);
	auto vertical = std::vector<float>(lowWidth * lowHeight);
	auto diagonal = std::vector<float>(lowWidth * lowHeight);
	for (auto &plane : planes) {
		// One level of the Haar wavelet (pywt.dwt2 'haar').
		for (auto y = 0; y != lowHeight; ++y) {
			for (auto x = 0; x != lowWidth; ++x) {
				const auto a = plane[(2 * y) * paddedWidth + 2 * x];
				const auto b = plane[(2 * y) * paddedWidth + 2 * x + 1];
				const auto c = plane[(2 * y + 1) * paddedWidth + 2 * x];
				const auto d = plane[(2 * y + 1) * paddedWidth + 2 * x + 1];
				const auto index = y * lowWidth + x;
				low[index] = (a + b + c + d) / 2.f;
				horizontal[index] = (a + b - c - d) / 2.f;
				vertical[index] = (a - b + c - d) / 2.f;
				diagonal[index] = (a - b - c + d) / 2.f;
			}
		}

		for (auto i = 0; i != blockCount; ++i) {
			const auto top = (i / blocksX) * kBlock;
			const auto left = (i % blocksX) * kBlock;
			auto block = Matrix();
			for (auto y = 0; y != kBlock; ++y) {
				for (auto x = 0; x != kBlock; ++x) {
					block[y][x] = low[(top + y) * lowWidth + left + x];
				}
			}
			const auto bit = watermark[i % watermark.size()];
			const auto embedded = EmbedBlock(block, permutations[i], bit);
			for (auto y = 0; y != kBlock; ++y) {
				for (auto x = 0; x != kBlock; ++x) {
					low[(top + y) * lowWidth + left + x] = float(embedded[y][x]);
				}
			}
		}

		// Inverse Haar.
		for (auto y = 0; y != lowHeight; ++y) {
			for (auto x = 0; x != lowWidth; ++x) {
				const auto index = y * lowWidth + x;
				const auto l = low[index];
				const auto h = horizontal[index];
				const auto v = vertical[index];
				const auto d = diagonal[index];
				plane[(2 * y) * paddedWidth + 2 * x] = (l + h + v + d) / 2.f;
				plane[(2 * y) * paddedWidth + 2 * x + 1] = (l + h - v - d) / 2.f;
				plane[(2 * y + 1) * paddedWidth + 2 * x] = (l - h + v - d) / 2.f;
				plane[(2 * y + 1) * paddedWidth + 2 * x + 1]
					= (l - h - v + d) / 2.f;
			}
		}
	}

	const auto clamp = [](float value) {
		return int(std::lround(std::clamp(value, 0.f, 255.f)));
	};
	for (auto y = 0; y != height; ++y) {
		const auto line = reinterpret_cast<QRgb*>(rgb.scanLine(y));
		for (auto x = 0; x != width; ++x) {
			const auto index = y * paddedWidth + x;
			const auto luma = planes[0][index];
			const auto u = planes[1][index] - 0.5f;
			const auto v = planes[2][index] - 0.5f;
			const auto r = luma + 1.140f * v;
			const auto g = luma - 0.395f * u - 0.581f * v;
			const auto b = luma + 2.032f * u;
			line[x] = qRgb(clamp(r), clamp(g), clamp(b));
		}
	}
	rgb.setDevicePixelRatio(ratio);
	image = std::move(rgb);
	return true;
}

} // namespace Ui::BlindWatermark
