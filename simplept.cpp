#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <cmath>
#include <random>
#include <vector>
#include <omp.h>
#define PI 3.1415926535897932384626433832795



/*
* Thread-safe random number generator
*/

struct RNG {
	RNG() : distrb(0.0, 1.0), engines() {}

	void init(int nworkers) {
		std::random_device rd;
		engines.resize(nworkers);
		for (int i = 0; i < nworkers; ++i)
			engines[i].seed(rd());
	}

	double operator()() {
		int id = omp_get_thread_num();
		return distrb(engines[id]);
	}

	std::uniform_real_distribution<double> distrb;
	std::vector<std::mt19937> engines;
} rng;


/*
* Basic data types
*/

struct Vec {
	double x, y, z;

	Vec(double x_ = 0, double y_ = 0, double z_ = 0) { x = x_; y = y_; z = z_; }

	Vec operator+ (const Vec &b) const { return Vec(x + b.x, y + b.y, z + b.z); }
	Vec operator- (const Vec &b) const { return Vec(x - b.x, y - b.y, z - b.z); }
	Vec operator* (double b) const { return Vec(x*b, y*b, z*b); }

	Vec mult(const Vec &b) const { return Vec(x*b.x, y*b.y, z*b.z); }
	Vec& normalize() { return *this = *this * (1.0 / std::sqrt(x*x + y*y + z*z)); }
	double dot(const Vec &b) const { return x*b.x + y*b.y + z*b.z; }
	Vec cross(const Vec&b) const { return Vec(y*b.z - z*b.y, z*b.x - x*b.z, x*b.y - y*b.x); }
};

struct Ray {
	Vec o, d;
	Ray(Vec o_, Vec d_) : o(o_), d(d_) {}
};

struct BRDF {
	virtual bool isSpecular() const = 0;
	virtual Vec eval(const Vec &n, const Vec &o, const Vec &i) const = 0;
	virtual void sample(const Vec &n, const Vec &o, Vec &i, double &pdf) const = 0;
};


/*
* Utility functions
*/

inline double clamp(double x) {
	return x < 0 ? 0 : x > 1 ? 1 : x;
}

inline int toInt(double x) {
	return static_cast<int>(std::pow(clamp(x), 1.0 / 2.2) * 255 + .5);
}


/*
* Shapes
*/

struct Sphere {
	Vec p, e;           // position, emitted radiance
	double rad;         // radius
	const BRDF &brdf;   // BRDF

	Sphere(double rad_, Vec p_, Vec e_, const BRDF &brdf_) :
		rad(rad_), p(p_), e(e_), brdf(brdf_) {}

	double intersect(const Ray &r) const { // returns distance, 0 if nohit
		Vec op = p - r.o; // Solve t^2*d.d + 2*t*(o-p).d + (o-p).(o-p)-R^2 = 0
		double t, eps = 1e-4, b = op.dot(r.d), det = b*b - op.dot(op) + rad*rad;
		if (det<0) return 0; else det = sqrt(det);
		return (t = b - det)>eps ? t : ((t = b + det)>eps ? t : 0);
	}
};


/*
* Sampling functions
*/

inline void createLocalCoord(const Vec &n, Vec &u, Vec &v, Vec &w) {
	w = n;
	u = ((std::abs(w.x)>.1 ? Vec(0, 1) : Vec(1)).cross(w)).normalize();
	v = w.cross(u);
}

void uniformRandomPSA(const Vec &n, const Vec &o, Vec &i, double &pdf) {
	double z = sqrt(rng());
	double r = sqrt(1.0 - z * z);
	double phi = 2.0 * PI * rng();
	double x = r * cos(phi);
	double y = r * sin(phi);

	Vec u, v, w;
	createLocalCoord(n, u, v, w);
	i = (u * x) + (v * y) + (w * z);
	i.normalize();
	pdf = n.dot(i) / PI;
}

/*
* BRDFs
*/

// Ideal diffuse BRDF
struct DiffuseBRDF : public BRDF {
	DiffuseBRDF(Vec kd_) : kd(kd_) {}

	bool isSpecular() const {
		return false;
	}

	Vec eval(const Vec &n, const Vec &o, const Vec &i) const {
		return kd * (1.0 / PI);
	}

	void sample(const Vec &n, const Vec &o, Vec &i, double &pdf) const {
		uniformRandomPSA(n, o, i, pdf);
	}

	Vec kd;
};

struct SpecularBRDF : public BRDF {
	SpecularBRDF(Vec ks_) : ks(ks_) {}

	bool isSpecular() const {
		return true;
	}

	void mirroredDirection(const Vec &n, const Vec &o, Vec &i) const {
		i = n * 2.0 * n.dot(o) - o;
	}

	Vec eval(const Vec&n, const Vec &o, const Vec &i) const {
		Vec mirrored;
		mirroredDirection(n, o, mirrored);
		Vec i_n = i;
		mirrored.normalize();
		i_n.normalize();

		if (abs(i_n.x - mirrored.x) <= 1e-4 &&
			abs(i_n.y - mirrored.y) <= 1e-4 &&
			abs(i_n.z - mirrored.z) <= 1e-4)
			return ks * (1 / n.dot(i));

		return Vec();
	}

	void sample(const Vec &n, const Vec&o, Vec&i, double &pdf) const {
		mirroredDirection(n, o, i);
		pdf = 1.0;
	}

	Vec ks;
};


/*
* Scene configuration
*/

// Pre-defined BRDFs
const DiffuseBRDF leftWall(Vec(.75, .25, .25)),
rightWall(Vec(.25, .25, .75)),
otherWall(Vec(.75, .75, .75)),
blackSurf(Vec(0.0, 0.0, 0.0)),
brightSurf(Vec(0.9, 0.9, 0.9));

const SpecularBRDF shinySurf(Vec(0.999, 0.999, 0.999));

// Scene: list of spheres
const Sphere spheres[] = {
	Sphere(1e5,  Vec(1e5 + 1,40.8,81.6),   Vec(),         leftWall),   // Left
	Sphere(1e5,  Vec(-1e5 + 99,40.8,81.6), Vec(),         rightWall),  // Right
	Sphere(1e5,  Vec(50,40.8, 1e5),      Vec(),         otherWall),  // Back
	Sphere(1e5,  Vec(50, 1e5, 81.6),     Vec(),         otherWall),  // Bottom
	Sphere(1e5,  Vec(50,-1e5 + 81.6,81.6), Vec(),         otherWall),  // Top
	Sphere(16.5, Vec(27,16.5,47),        Vec(),         brightSurf), // Ball 1
	Sphere(16.5, Vec(73,16.5,78),        Vec(),         shinySurf), // Ball 2
	Sphere(5.0,  Vec(50,70.0,81.6),      Vec(50,50,50), blackSurf)   // Light
};

// Camera position & direction
const Ray cam(Vec(50, 52, 295.6), Vec(0, -0.042612, -1).normalize());


/*
* Global functions
*/

bool intersect(const Ray &r, double &t, int &id) {
	double n = sizeof(spheres) / sizeof(Sphere), d, inf = t = 1e20;
	for (int i = int(n); i--;) if ((d = spheres[i].intersect(r)) && d<t) { t = d; id = i; }
	return t<inf;
}

void luminaireSample(const Sphere &source, Vec &surfacePoint, Vec &n, double &pdf) {
	double r = source.rad,
		e1 = rng(), e2 = rng(),
		z = 2.0 * e1 - 1.0,
		x = sqrt(1.0 - (z * z)) * cos(2.0 * PI * e2),
		y = sqrt(1.0 - (z * z)) * sin(2.0 * PI * e2);

	surfacePoint = source.p + (Vec(x, y, z) * r);
	n = Vec(x, y, z);
	pdf = 1 / (4.0 * PI * (r * r));
}


double isVisibile(const Vec& x, const Vec&y) {

	Ray r(x, (y - x).normalize());

	double t;
	int id = 0;

	intersect(r, t, id);
	//intersection point
	Vec ip = r.o + r.d * t;

	//if the intersection point is the same point as y, return 1, else 0
	if (abs(ip.x - y.x) <= 1e-4 &&
		abs(ip.y - y.y) <= 1e-4 &&
		abs(ip.z - y.z) <= 1e-4)
		return 1.0;
	return 0.0;
}
/*
* KEY FUNCTION: radiance estimator
*/

Vec directRadiance(const Sphere &obj, Ray &r, Vec &n) {
	Vec directRad;
	if (!obj.brdf.isSpecular()) {
		Vec y1;				//surface point on light source
		Vec sourceNormal;	//normal at surface point
		double pdf1;		//pdf of light source

		//light source hard coded in this example to be 7th sphere in scene
		//if more light sources present take them as parameters and loop 
		//over them to get individual contributions?

		luminaireSample(spheres[7], y1, sourceNormal, pdf1);
		Vec o1 = (y1 - r.o).normalize();
		double r2 = (r.o - y1).dot(r.o - y1);

		directRad = spheres[7].e.mult(obj.brdf.eval(n, o1, r.d)) * isVisibile(r.o, y1)
			* (n.dot(o1) * sourceNormal.dot(o1 * -1) / (r2 * pdf1));
	}

	return directRad;
}

Vec receivedRadiance(const Ray &r, int depth, bool flag) {
	double t;                                   // Distance to intersection
	int id = 0;                                 // id of intersected sphere

	if (!intersect(r, t, id)) return Vec();   // if miss, return black
	const Sphere &obj = spheres[id];            // the hit object

	Vec x = r.o + r.d*t;                        // The intersection point
	Vec o = (Vec() - r.d).normalize();          // The outgoing direction (= -r.d)

	Vec n = (x - obj.p).normalize();            // The normal direction
	if (n.dot(o) < 0) n = n*-1.0;

	/*
	Tips

	1. Other useful quantities/variables:
	Vec Le = obj.e;                             // Emitted radiance
	const BRDF &brdf = obj.brdf;                // Surface BRDF at x

	2. Call brdf.sample() to sample an incoming direction and continue the recursion
	*/

	Vec totalRad; //the sum of direct radiance (if we use it) and the indirect radiance 
	if (!obj.brdf.isSpecular()) {
		Ray out(x, o);
		totalRad = directRadiance(obj, out, n);
	}

	double p = 1.0;
	if (depth >= 5)
		p = 0.9;

	if (rng() < p) {

		//sample a new incoming direction at the surface point
		Vec i;
		double pdf;
		obj.brdf.sample(n, o, i, pdf);

		//create a Ray from the surface point and the sampled direction and ray trace
		Ray y(x, i.normalize());
		totalRad = totalRad + receivedRadiance(y, depth + 1, obj.brdf.isSpecular())
			.mult(obj.brdf.eval(n, o, i)) * (n.dot(i) / (pdf * p));

	}

	return flag ? obj.e + totalRad : totalRad;

	/*Ray out(x, o);

	return directRad + radiance(obj, out, n, depth, flag);*/

}


/*
* Main function (do not modify)
*/

int main(int argc, char *argv[]) {
	int nworkers = omp_get_num_procs();
	omp_set_num_threads(nworkers);
	rng.init(nworkers);

	int w = 480, h = 360, samps = argc == 2 ? atoi(argv[1]) / 4 : 1; // # samples
	Vec cx = Vec(w*.5135 / h), cy = (cx.cross(cam.d)).normalize()*.5135;
	std::vector<Vec> c(w*h);

#pragma omp parallel for schedule(dynamic, 1)
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			const int i = (h - y - 1)*w + x;

			for (int sy = 0; sy < 2; ++sy) {
				for (int sx = 0; sx < 2; ++sx) {
					Vec r;
					for (int s = 0; s<samps; s++) {
						double r1 = 2 * rng(), dx = r1<1 ? sqrt(r1) - 1 : 1 - sqrt(2 - r1);
						double r2 = 2 * rng(), dy = r2<1 ? sqrt(r2) - 1 : 1 - sqrt(2 - r2);
						Vec d = cx*(((sx + .5 + dx) / 2 + x) / w - .5) +
							cy*(((sy + .5 + dy) / 2 + y) / h - .5) + cam.d;
						r = r + receivedRadiance(Ray(cam.o, d.normalize()), 1, true)*(1. / samps);
					}
					c[i] = c[i] + Vec(clamp(r.x), clamp(r.y), clamp(r.z))*.25;
				}
			}
		}
#pragma omp critical
		fprintf(stderr, "\rRendering (%d spp) %6.2f%%", samps * 4, 100.*y / (h - 1));
	}
	fprintf(stderr, "\n");

	// Write resulting image to a PPM file
	FILE *f = fopen("image.ppm", "w");
	fprintf(f, "P3\n%d %d\n%d\n", w, h, 255);
	for (int i = 0; i<w*h; i++)
		fprintf(f, "%d %d %d ", toInt(c[i].x), toInt(c[i].y), toInt(c[i].z));
	fclose(f);

	return 0;
}