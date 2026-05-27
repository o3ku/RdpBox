#include <cassert>

#include "common/NativeTypes.h"
#include "profiles/Profile.h"

int main()
{
    {
        Profile profile = Profile::create();
        assert(!profile.isValid());

        profile.name = L"server";
        profile.host = L"10.0.0.20";
        assert(profile.isValid());

        profile.name.clear();
        assert(!profile.isValid());
    }

    {
        SizeI a{640, 480};
        SizeI b{640, 480};
        SizeI c{800, 600};
        assert(a == b);
        assert(!(a == c));
    }

    {
        FrameBuffer frame;
        assert(frame.empty());

        frame.width = 100;
        frame.height = 50;
        frame.stride = 400;
        frame.pixels.resize(20000u);
        assert(!frame.empty());

        frame.pixels.clear();
        assert(frame.empty());
    }

    {
        CursorInfo cursor;
        assert(cursor.kind == CursorKind::Arrow);
        assert(cursor.handle == nullptr);
        assert(!cursor.ownsHandle);
    }

    return 0;
}
