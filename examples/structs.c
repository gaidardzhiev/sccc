#include <stdio.h>
#include <string.h>
#include <stddef.h>

typedef enum { CIRCLE, RECT, TRIANGLE } Kind;

typedef struct {
	double x, y;
} Point;

typedef struct {
	Kind kind;
	const char *name;
	Point origin;
	union {
		double radius;
		struct { double w, h; } rect;
		Point tri[3];
	} u;
} Shape;

typedef struct {
	unsigned ready : 1;
	unsigned mode : 3;
	unsigned count : 12;
	signed delta : 5;
} Flags;

static double area(const Shape *s) {
	switch (s->kind) {
	case CIRCLE: return 3.14159265358979 * s->u.radius * s->u.radius;
	case RECT: return s->u.rect.w * s->u.rect.h;
	case TRIANGLE: {
		const Point *p = s->u.tri;
		double a = (p[1].x - p[0].x) * (p[2].y - p[0].y) - (p[2].x - p[0].x) * (p[1].y - p[0].y);
		return a < 0 ? -a / 2 : a / 2;
	}
	}
	return 0;
}

static Point add(Point a, Point b) {
	Point r = { a.x + b.x, a.y + b.y };
	return r;
}

int main(void) {
	Shape shapes[] = {
		{ .kind = CIRCLE, .name = "circle", .u.radius = 2 },
		{ .kind = RECT, .name = "rect", .origin = { 1, 1 }, .u.rect = { 3, 4.5 } },
		{ .kind = TRIANGLE, .name = "triangle", .u.tri = { { 0, 0 }, { 4, 0 }, { 0, 3 } } },
	};
	for (size_t i = 0; i < sizeof shapes / sizeof shapes[0]; i++)
		printf("%-8s at (%g,%g) area %.3f\n", shapes[i].name, shapes[i].origin.x, shapes[i].origin.y, area(&shapes[i]));

	Point p = add((Point){ 1.5, 2 }, (Point){ .y = 10, .x = -1 });
	printf("point sum (%g, %g)\n", p.x, p.y);

	Shape copy = shapes[1];
	copy.u.rect.w = 10;
	printf("copy w=%g, original w=%g\n", copy.u.rect.w, shapes[1].u.rect.w);

	Flags f = { 1, 5, 4000, -7 };
	printf("flags: ready=%u mode=%u count=%u delta=%d\n", f.ready, f.mode, f.count, f.delta);
	f.count += 100;
	f.mode = f.mode + 4;
	f.delta--;
	printf("flags: ready=%u mode=%u count=%u delta=%d\n", f.ready, f.mode, f.count, f.delta);

	int grid[3][4] = { [0] = { 1, 2 }, [2][3] = 9, [1] = { [1] = 5 } };
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 4; j++) printf("%d ", grid[i][j]);
		printf("\n");
	}
	printf("sizeof Point %zu, Shape %zu, offsetof(Shape, u) %zu\n", sizeof(Point), sizeof(Shape), offsetof(Shape, u));
	union { unsigned u; unsigned char b[4]; } endian = { 0x01020304 };
	printf("byte order: %d %d %d %d\n", endian.b[0], endian.b[1], endian.b[2], endian.b[3]);
	return 0;
}
