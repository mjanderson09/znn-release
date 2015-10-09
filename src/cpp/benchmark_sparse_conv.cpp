//
// Copyright (C) 2012-2015  Aleksandar Zlateski <zlateski@mit.edu>
// ---------------------------------------------------------------
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#include "network/parallel/network.hpp"

using namespace znn::v4;

int main(int argc, char** argv)
{
    int64_t x = 9;
    int64_t y = 9;
    int64_t z = 9;

    int64_t fx = 3;
    int64_t fy = 3;
    int64_t fz = 3;

    int64_t sx = 2;
    int64_t sy = 2;
    int64_t sz = 2;


    if ( argc >= 4 )
    {
        x = atoi(argv[1]);
        y = atoi(argv[2]);
        z = atoi(argv[3]);
    }

    if ( argc >= 7 )
    {
        fx = atoi(argv[4]);
        fy = atoi(argv[5]);
        fz = atoi(argv[6]);
    }

    if ( argc >= 10 )
    {
        sx = atoi(argv[7]);
        sy = atoi(argv[8]);
        sz = atoi(argv[9]);
    }

    size_t tc = 10;

    if ( argc == 11 )
    {
        tc = atoi(argv[10]);
    }

    auto v = get_cube<real>(vec3i(x,y,z));
    auto f = get_cube<real>(vec3i(fx,fy,fz));

    for ( int i = 0; i < v->num_elements(); ++i )
        v->data()[i] = i;

    for ( int i = 0; i < f->num_elements(); ++i )
        f->data()[i] = 0.001 * i;


    vec3i s(sx,sy,sz);

    auto v2 = convolve_sparse(v,f,s);
    //std::cout << *v2 << std::endl << std::endl;
    v = convolve_sparse_inverse(v2,f,s);

    zi::wall_timer wt;
    wt.reset();

    for ( size_t i = 0; i < tc; ++i )
    {
        v2 = convolve_sparse(v,f,s);
        //v = convolve_sparse_inverse(v,f,s);
    }

    std::cout << "Elapsed: " << wt.elapsed<double>() << std::endl;
    std::cout << "Sum: " << sum(*v2) << std::endl;

}
