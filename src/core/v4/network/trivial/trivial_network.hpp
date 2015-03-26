#pragma once

#include "../../assert.hpp"
#include "../../cube/cube.hpp"
#include "../../cube/cube_operators.hpp"
#include "../../pooling/pooling.hpp"
#include "../../initializator/initializators.hpp"
#include "../../options/options.hpp"
#include "../../convolution/convolution.hpp"
#include "../../transfer_function/transfer_functions.hpp"
#include "../../types.hpp"
#include "../bias.hpp"
#include "../filter.hpp"
#include "utils.hpp"

#include <map>
#include <string>
#include <vector>

namespace znn { namespace v4 { namespace trivial_network {

// Forward definition
class edge;

class nodes
{
public:
    virtual ~nodes() {}

    // receive a featuremap for the i-th input
    // featuremap is absorbed
    virtual void forward(size_t, cube_p<double>&&)
    { UNIMPLEMENTED(); }

    // receive a gradient for the i-th output
    // gradient is absorbed
    virtual void backward(size_t, cube_p<double>&&)
    { UNIMPLEMENTED(); }

    virtual std::vector<cube_p<double>>& get_featuremaps()
    { UNIMPLEMENTED(); }

    virtual size_t num_out_nodes()
    { UNIMPLEMENTED(); }

    virtual size_t num_in_nodes()
    { UNIMPLEMENTED(); }

    virtual void attach_out_edge(size_t, edge*)
    { UNIMPLEMENTED(); }

    virtual void attach_in_edge(size_t, edge*)
    { UNIMPLEMENTED(); }

    virtual void set_eta( double )
    { UNIMPLEMENTED(); }

    virtual void set_momentum( double )
    { UNIMPLEMENTED(); }

    virtual void set_weight_decay( double )
    { UNIMPLEMENTED(); }

    virtual void initialize()
    { UNIMPLEMENTED(); }

    virtual options serialize() const = 0;

};

class edge
{
public:
    virtual ~edge() {}

    // perform forward computation
    // can't modify the featuremap
    virtual void forward( ccube_p<double> const & ) = 0;

    // perform forward computation
    // can't modify the gradient
    virtual void backward( ccube_p<double> const & ) = 0;
};

class edge_base: public virtual edge
{
protected:
    nodes * in_nodes ;
    size_t  in_num   ;
    nodes * out_nodes;
    size_t  out_num  ;

public:
    edge_base( nodes* in,
               size_t inn,
               nodes* out,
               size_t outn )
        : in_nodes(in)
        , in_num(inn)
        , out_nodes(out)
        , out_num(outn)
    {
    }
};



template< typename E >
class edge_of: public edge_base
{
private:
    E impl;

public:
    template<class... Args>
    edge_of( nodes* in,
             size_t inn,
             nodes* out,
             size_t outn,
             Args&&... args )
        : edge_base(in, inn, out, outn)
        , impl(std::forward<Args>(args)...)
    {
        // attach myself
        in->attach_out_edge(inn, this);
        out->attach_in_edge(outn, this);
    }

    void forward( ccube_p<double> const & f ) override
    {
        out_nodes->forward(out_num, impl.forward(f));
    }

    void backward( ccube_p<double> const & g ) override
    {
        in_nodes->backward(in_num, impl.backward(g));
    }
};

struct dummy_edge
{
    cube_p<double> forward( ccube_p<double> const & f )
    {
        return get_copy(*f);
    }

    cube_p<double> backward( ccube_p<double> const & g )
    {
        return get_copy(*g);
    }
};

class max_pooling_edge
{
private:
    vec3i filter_size;
    vec3i filter_stride;

    cube_p<int> indices;
    vec3i       insize ;

public:
    max_pooling_edge( vec3i const & size,
                      vec3i const & stride )
        : filter_size(size), filter_stride(stride)
    {
    }

    cube_p<double> forward( ccube_p<double> const & f )
    {
        insize = size(*f);
        auto r = pooling_filter(get_copy(*f),
                                [](double a, double b){ return a>b; },
                                filter_size,
                                filter_stride);
        indices = r.second;
        return r.first;
    }

    cube_p<double> backward( ccube_p<double> const & g )
    {
        ZI_ASSERT(indices);
        ZI_ASSERT(insize == size(*g) + (filter_size - vec3i::one) * filter_stride);

        return pooling_backprop(insize, *g, *indices);
    }
};


class filter_edge
{
private:
    vec3i    filter_stride;
    filter & filter_;

    ccube_p<double> last_input;

public:
    filter_edge( vec3i const & stride, filter & f )
        : filter_stride(stride), filter_(f)
    {
    }

    cube_p<double> forward( ccube_p<double> const & f )
    {
        last_input = f;
        return convolve_sparse(*f, filter_.W(), filter_stride);
    }

    cube_p<double> backward( ccube_p<double> const & g )
    {
        ZI_ASSERT(last_input);
        auto dEdW = convolve_sparse_flipped(*last_input, *g, filter_stride);
        auto ret  = convolve_sparse_inverse(*g, filter_.W(), filter_stride);
        filter_.update(*dEdW);


        //std::cout << "FILTER: " <<  filter_.W() << std::endl;

        return ret;
    }
};

class edges
{
public:
    virtual ~edges() {}

    virtual void set_eta( double )
    { UNIMPLEMENTED(); }

    virtual void set_momentum( double )
    { UNIMPLEMENTED(); }

    virtual void set_weight_decay( double )
    { UNIMPLEMENTED(); }

    virtual options serialize() const = 0;
};

class dummy_edges: public edges
{
private:
    options                                    options_;
    std::vector<std::unique_ptr<edge>> edges_  ;

public:
    dummy_edges( nodes * in,
                 nodes * out,
                 options const & opts )
        : options_(opts)
    {
        ZI_ASSERT(in->num_out_nodes()==out->num_in_nodes());

        size_t n = in->num_out_nodes();
        edges_.resize(n);

        for ( size_t i = 0; i < n; ++i )
        {
            edges_[i] = std::make_unique<edge_of<dummy_edge>>
                (in, i, out, i);
        }
    }

    void set_eta( double ) {}
    void set_momentum( double ) {}
    void set_weight_decay( double ) {}

    options serialize() const
    {
        return options_;
    }
};

class max_pooling_edges: public edges
{
private:
    options                                    options_;
    std::vector<std::unique_ptr<edge>> edges_  ;

public:
    max_pooling_edges( nodes * in,
                       nodes * out,
                       options const & opts,
                       vec3i const & stride )
        : options_(opts)
    {
        ZI_ASSERT(in->num_out_nodes()==out->num_in_nodes());

        size_t n = in->num_out_nodes();
        edges_.resize(n);

        auto sz     = opts.require_as<ovec3i>("size");

        for ( size_t i = 0; i < n; ++i )
        {
            edges_[i]
                = std::make_unique<edge_of<max_pooling_edge>>
                (in, i, out, i, sz, stride);
        }
    }

    void set_eta( double ) {}
    void set_momentum( double ) {}
    void set_weight_decay( double ) {}

    options serialize() const
    {
        return options_;
    }
};


class filter_edges: public edges
{
private:
    options                                           options_;
    std::vector<std::unique_ptr<filter>>              filters_;
    std::vector<std::unique_ptr<edge>>        edges_  ;
    vec3i                                             size_   ;

public:
    filter_edges( nodes * in,
                  nodes * out,
                  options const & opts,
                  vec3i const & stride )
        : options_(opts)
    {
        size_t n = in->num_out_nodes();
        size_t m = out->num_in_nodes();

        ZI_ASSERT((n>0)&&m>0);

        edges_.resize(n*m);
        filters_.resize(n*m);

        double eta    = opts.optional_as<double>("eta", 0.1);
        double mom    = opts.optional_as<double>("momentum", 0.0);
        double wd     = opts.optional_as<double>("weight_decay", 0.0);
        auto   sz     = opts.require_as<ovec3i>("size");

        size_ = sz;

        for ( size_t i = 0, k = 0; i < n; ++i )
        {
            for ( size_t j = 0; j < m; ++j, ++k )
            {
                filters_[k] = std::make_unique<filter>(sz, eta, mom, wd);
                edges_[k]
                    = std::make_unique<edge_of<filter_edge>>
                    (in, i, out, j, stride, *filters_[k]);
            }
        }

        std::string filter_values;

        opts.dump();

        if ( opts.contains("filters") )
        {
            filter_values = opts.require_as<std::string>("filters");
        }
        else
        {
            size_t n_values = n*m*size_[0]*size_[1]*size_[2];
            double * filters_raw = new double[n_values];

            auto initf = get_initializator(opts);

            //std::cout << "here\n";

            initf->initialize( filters_raw, n*m*size_[0]*size_[1]*size_[2] );
            delete [] filters_raw;

            filter_values = std::string( reinterpret_cast<char*>(filters_raw),
                                         sizeof(double) * n_values );
        }

        load_filters(filters_, size_, filter_values);
    }

    void set_eta( double eta ) override
    {
        options_.push("eta", eta);
        for ( auto & f: filters_ ) f->eta() = eta;
    }

    void set_momentum( double mom ) override
    {
        options_.push("momentum", mom);
        for ( auto & f: filters_ ) f->momentum() = mom;
    }

    void set_weight_decay( double wd ) override
    {
        options_.push("weight_decay", wd);
        for ( auto & f: filters_ ) f->weight_decay() = wd;
    }

    options serialize() const
    {
        options ret = options_;
        ret.push("filters", save_filters(filters_, size_));
        return ret;
    }
};



class input_nodes: public nodes
{
private:
    size_t                                  size_   ;
    std::vector<std::vector<edge*>> outputs_;
    options                                 opts_   ;

public:
    input_nodes(size_t s, options const & op)
        : size_(s)
        , outputs_(s)
        , opts_(op)
    {}

    void forward(size_t n, cube_p<double>&& f) override
    {
        ZI_ASSERT(n<size_);
        for ( auto& e: outputs_[n] )
        {
            e->forward(f);
        }
    }

    void backward(size_t, cube_p<double>&&) override
    {
        //std::cout << "input node: " << i << " received grad of size: "
        //          << size(*g) << std::endl;
    }

    size_t num_out_nodes() override { return size_; }
    size_t num_in_nodes()  override { return size_; }

    void attach_out_edge(size_t i, edge* e) override
    {
        ZI_ASSERT(i<size_);
        outputs_[i].push_back(e);
    }

    void set_eta( double ) override {}
    void set_momentum( double ) override {}
    void set_weight_decay( double ) override {}

    options serialize() const
    {
        return opts_;
    }
};


class summing_nodes: public nodes
{
private:
    size_t                                  size_    ;
    std::vector<std::vector<edge*>> inputs_  ;
    std::vector<std::vector<edge*>> outputs_ ;
    std::vector<size_t>                     received_;
    std::vector<cube_p<double>>             fs_      ;
    std::vector<cube_p<double>>             gs_      ;
    options                                 opts_   ;

public:
    summing_nodes(size_t s, options const & op)
        : size_(s)
        , inputs_(s)
        , outputs_(s)
        , received_(s)
        , fs_(s)
        , gs_(s)
        , opts_(op)
    {}

    void set_eta( double ) override {}
    void set_momentum( double ) override {}
    void set_weight_decay( double ) override {}

    options serialize() const
    {
        return opts_;
    }

    std::vector<cube_p<double>>& get_featuremaps() override
    {
        return fs_;
    }

    void forward(size_t n, cube_p<double>&& f) override
    {
        ZI_ASSERT(n<size_);
        if ( received_[n] == 0 )
        {
            fs_[n] = f;
        }
        else
        {
            *fs_[n] += *f;
        }

        if ( ++received_[n] == inputs_[n].size() )
        {
            for ( auto& e: outputs_[n] )
            {
                e->forward(fs_[n]);
            }
            received_[n] = 0;
            fs_[n].reset();
        }
    }

    void backward(size_t n, cube_p<double>&& g) override
    {
        ZI_ASSERT(n<size_);
        if ( received_[n] == 0 )
        {
            gs_[n] = g;
        }
        else
        {
            *gs_[n] += *g;
        }

        if (( ++received_[n] == outputs_[n].size() ) ||
            ( outputs_[n].size() == 0 ))
        {
            for ( auto& e: inputs_[n] )
            {
                e->backward(gs_[n]);
            }
            received_[n] = 0;
            gs_[n].reset();
        }
    }

    size_t num_out_nodes() override { return size_; }
    size_t num_in_nodes()  override { return size_; }

    void attach_in_edge(size_t i, edge* e) override
    {
        ZI_ASSERT(i<size_);
        inputs_[i].push_back(e);
    }

    void attach_out_edge(size_t i, edge* e) override
    {
        ZI_ASSERT(i<size_);
        outputs_[i].push_back(e);
    }
};


class transfer_nodes: public nodes
{
private:
    size_t                                  size_    ;
    std::vector<std::unique_ptr<bias>>      biases_  ;
    transfer_function                       func_    ;
    std::vector<std::vector<edge*>> inputs_  ;
    std::vector<std::vector<edge*>> outputs_ ;
    std::vector<size_t>                     received_;
    std::vector<cube_p<double>>             fs_      ;
    std::vector<cube_p<double>>             gs_      ;
    options                                 options_ ;

public:
    transfer_nodes( options const & opts )
        : size_(opts.require_as<size_t>("size"))
        , biases_(size_)
        , func_()
        , inputs_(size_)
        , outputs_(size_)
        , received_(size_)
        , fs_(size_)
        , gs_(size_)
        , options_(opts)
    {
        func_ = get_transfer_function(opts);

        // initialize biases

        double eta = opts.optional_as<double>("eta", 0.1);
        double mom = opts.optional_as<double>("momentum", 0.0);
        double wd  = opts.optional_as<double>("weight_decay", 0.0);

        for ( auto& b: biases_ )
        {
            b = std::make_unique<bias>(eta, mom, wd);
        }

        std::string bias_values;

        if ( opts.contains("biases") )
        {
            bias_values = opts.require_as<std::string>("biases");
        }
        else
        {
            double biases_raw[size_];
            if ( opts.contains("init") )
            {
                auto initf = get_initializator(opts);
                initf->initialize( biases_raw, size_ );
            }
            else
            {
                std::fill_n(biases_raw, size_, 0);
            }

            bias_values = std::string( reinterpret_cast<char*>(biases_raw),
                                       sizeof(double) * size_ );
        }

        load_biases(biases_, bias_values);
    }

    void set_eta( double eta ) override
    {
        options_.push("eta", eta);
        for ( auto& b: biases_ ) b->eta() = eta;
    }

    void set_momentum( double mom ) override
    {
        options_.push("momentum", mom);
        for ( auto& b: biases_ ) b->momentum() = mom;
    }

    void set_weight_decay( double wd ) override
    {
        options_.push("weight_decay", wd);
        for ( auto& b: biases_ ) b->weight_decay() = wd;
    }

    options serialize() const
    {
        options ret = options_;
        ret.push("biases", save_biases(biases_));
        return ret;
    }

    std::vector<cube_p<double>>& get_featuremaps() override
    {
        return fs_;
    }

    void forward(size_t n, cube_p<double>&& f) override
    {
        ZI_ASSERT(n<size_);
        if ( received_[n] == 0 )
        {
            fs_[n] = f;
        }
        else
        {
            *fs_[n] += *f;
        }

        if ( ++received_[n] == inputs_[n].size() )
        {

            func_.apply(*fs_[n], biases_[n]->b());
            for ( auto& e: outputs_[n] )
            {
                e->forward(fs_[n]);
            }
            received_[n] = 0;
        }
    }

    void backward(size_t n, cube_p<double>&& g) override
    {
        ZI_ASSERT(n<size_);
        if ( received_[n] == 0 )
        {
            gs_[n] = g;
        }
        else
        {
            *gs_[n] += *g;
        }

        if ( (++received_[n] == outputs_[n].size()) ||
             (outputs_[n].size() == 0 ) )
        {
            func_.apply_grad(*gs_[n], *fs_[n]);
            biases_[n]->update(sum(*gs_[n]));

            //std::cout << "Bias: " << options_.require_as<std::string>("name")
            //          << ' ' << n << ' ' << biases_[n]->b() << std::endl;

            for ( auto& e: inputs_[n] )
            {
                e->backward(gs_[n]);
            }

            received_[n] = 0;
            gs_[n].reset();
            fs_[n].reset();
        }
    }

    size_t num_out_nodes() override { return size_; }
    size_t num_in_nodes()  override { return size_; }

    void attach_in_edge(size_t i, edge* e) override
    {
        ZI_ASSERT(i<size_);
        inputs_[i].push_back(e);
    }

    void attach_out_edge(size_t i, edge* e) override
    {
        ZI_ASSERT(i<size_);
        outputs_[i].push_back(e);
    }
};

class network
{
private:
    struct nnodes;

    struct nedges
    {
        vec3i width     = vec3i::one  ;
        vec3i stride    = vec3i::one  ;

        vec3i in_stride = vec3i::zero ;
        vec3i in_fsize  = vec3i::zero ;

        nnodes * in;
        nnodes * out;

        options const * opts;

        std::unique_ptr<edges> edges;
    };

    struct nnodes
    {
        vec3i fov       = vec3i::zero;
        vec3i stride    = vec3i::zero;
        vec3i fsize     = vec3i::zero;

        std::unique_ptr<nodes> nodes;
        std::vector<nedges *> in, out;
    };

private:
    network( network const & ) = delete;
    network& operator=( network const & ) = delete;

    network( network && ) = delete;
    network& operator=( network && ) = delete;

public:
    ~network()
    {
        for ( auto& n: nodes_ ) delete n.second;
        for ( auto& e: edges_ ) delete e.second;
    }

private:
    std::map<std::string, nedges*> edges_;
    std::map<std::string, nnodes*> nodes_;
    std::map<std::string, nnodes*> input_nodes_;
    std::map<std::string, nnodes*> output_nodes_;

    void fov_pass(nnodes* n, const vec3i& fov, const vec3i& fsize )
    {
        if ( n->fov != vec3i::zero )
        {
            ZI_ASSERT(n->fsize==fsize);
            ZI_ASSERT(n->fov==fov);
        }
        else
        {
            for ( auto& e: n->out )
            {
                e->in_fsize = fsize;
            }
            n->fov = fov;
            n->fsize = fsize;
            for ( auto& e: n->in )
            {
                vec3i new_fov   = (fov - vec3i::one) * e->stride + e->width;
                vec3i new_fsize = (e->width-vec3i::one) * e->in_stride + fsize;
                fov_pass(e->in, new_fov, new_fsize);
            }
        }
    }

    void stride_pass(nnodes* n, const vec3i& stride )
    {
        if ( n->stride != vec3i::zero )
        {
            ZI_ASSERT(n->stride==stride);
        }
        else
        {
            n->stride = stride;
            for ( auto& e: n->out )
            {
                e->in_stride = stride;
                stride_pass(e->out, stride * e->stride );
            }
        }
    }

    void init( vec3i const& outsz )
    {
        for ( auto& o: nodes_ )
            if ( o.second->out.size() == 0 )
                output_nodes_[o.first] = o.second;

        for ( auto& o: input_nodes_ )
            stride_pass(o.second, vec3i::one);
        for ( auto& o: output_nodes_ )
            fov_pass(o.second, vec3i::one, outsz);

        for ( auto& o: nodes_ )
        {
            std::cout << "NODE GROUP: " << o.first << "\n    "
                      << "FOV: " << o.second->fov << "\n    "
                      << "STRIDE: " << o.second->stride << "\n    "
                      << "SIZE: " << o.second->fsize << '\n';
        }

        // for ( auto& o: edges_ )
        // {
        //     std::cout << o.first << ' ' << o.second->width
        //               << ' ' << o.second->stride
        //               << ' ' << o.second->in_stride << '\n';
        // }

    }

    void add_nodes( options const & op )
    {
        auto name = op.require_as<std::string>("name");
        auto type = op.require_as<std::string>("type");
        auto sz   = op.require_as<size_t>("size");

        ZI_ASSERT(sz>0);
        ZI_ASSERT(nodes_.count(name)==0);
        nnodes* ns   = new nnodes;
        nodes_[name] = ns;

        if ( type == "input" )
        {
            input_nodes_[name] = ns;
            ns->nodes = std::make_unique<input_nodes>(sz,op);
        }
        else if ( type == "sum" )
        {
            ns->nodes = std::make_unique<summing_nodes>(sz,op);
        }
        else if ( type == "transfer" )
        {
            ns->nodes = std::make_unique<transfer_nodes>(op);
        }
        else
        {
            throw std::logic_error(HERE() + "unknown nodes type: " + type);
        }
    }

    void create_edges()
    {
        for ( auto & e: edges_ )
        {
            auto type = e.second->opts->require_as<std::string>("type");
            nodes * in  = e.second->in->nodes.get();
            nodes * out = e.second->out->nodes.get();

            if ( type == "max_filter" )
            {
                e.second->edges = std::make_unique<max_pooling_edges>
                    ( in, out, *e.second->opts, e.second->in_stride );
            }
            else if ( type == "conv" )
            {
                e.second->edges = std::make_unique<filter_edges>
                    ( in, out, *e.second->opts, e.second->in_stride );
            }
            else if ( type == "dummy" )
            {
                e.second->edges = std::make_unique<dummy_edges>
                    ( in, out, *e.second->opts );
            }
            else
            {
                throw std::logic_error(HERE() + "unknown nodes type: " + type);
            }

            e.second->opts = nullptr;
        }
    }

    void add_edges( options const & op )
    {
        auto name = op.require_as<std::string>("name");
        auto type = op.require_as<std::string>("type");
        auto in   = op.require_as<std::string>("input");
        auto out  = op.require_as<std::string>("output");

        ZI_ASSERT(edges_.count(name)==0);
        ZI_ASSERT(nodes_.count(in)&&nodes_.count(out));

        nedges * es = new nedges;
        es->opts = &op;
        es->in   = nodes_[in];
        es->out  = nodes_[out];
        nodes_[in]->out.push_back(es);
        nodes_[out]->in.push_back(es);

        edges_[name] = es;

        if ( type == "max_filter" )
        {
            es->width  = op.require_as<ovec3i>("size");
            es->stride = op.require_as<ovec3i>("stride");
        }
        else if ( type == "conv" )
        {
            es->width  = op.require_as<ovec3i>("size");
            es->stride = op.optional_as<ovec3i>("stride", "1,1,1");
        }
        else if ( type == "dummy" )
        {
        }
        else
        {
            throw std::logic_error(HERE() + "unknown nodes type: " + type);
        }

    }


public:
    network( std::vector<options> const & ns,
             std::vector<options> const & es,
             vec3i const & outsz )
    {
        for ( auto& n: ns ) add_nodes(n);
        for ( auto& e: es ) add_edges(e);
        init(outsz);
        create_edges();
    }

    void set_eta( double eta )
    {
        for ( auto & e: edges_ ) e.second->edges->set_eta(eta);
        for ( auto & n: nodes_ ) n.second->nodes->set_eta(eta);
    }

    void set_momentum( double mom )
    {
        for ( auto & e: edges_ ) e.second->edges->set_momentum(mom);
        for ( auto & n: nodes_ ) n.second->nodes->set_momentum(mom);
    }

    void set_weight_decay( double wd )
    {
        for ( auto & e: edges_ ) e.second->edges->set_weight_decay(wd);
        for ( auto & n: nodes_ ) n.second->nodes->set_weight_decay(wd);
    }

    vec3i fov() const
    {
        return input_nodes_.begin()->second->fov;
    }

    std::map<std::string, std::vector<cube_p<double>>>
    forward( std::map<std::string, std::vector<cube_p<double>>> && fin )
    {
        ZI_ASSERT(fin.size()==input_nodes_.size());
        for ( auto & in: fin )
        {
            ZI_ASSERT(input_nodes_.count(in.first));

            auto& in_layer = input_nodes_[in.first]->nodes;

            ZI_ASSERT(in_layer->num_in_nodes()==in.second.size());

            for ( size_t i = 0; i < in.second.size(); ++i )
            {
                in_layer->forward(i, std::move(in.second[i]));
            }
        }

        std::map<std::string, std::vector<cube_p<double>>> ret;
        for ( auto & l: output_nodes_ )
        {
            //std::cout << "Collecting for: " << l.first << "\n";
            ret[l.first] = l.second->nodes->get_featuremaps();
        }

        return ret;
    }

    std::map<std::string, std::vector<cube_p<double>>>
    backward( std::map<std::string, std::vector<cube_p<double>>> && fout )
    {
        ZI_ASSERT(fout.size()==input_nodes_.size());
        for ( auto & out: fout )
        {
            ZI_ASSERT(output_nodes_.count(out.first));

            auto& out_layer = output_nodes_[out.first]->nodes;

            ZI_ASSERT(out_layer->num_out_nodes()==out.second.size());

            for ( size_t i = 0; i < out.second.size(); ++i )
            {
                out_layer->backward(i, std::move(out.second[i]));
            }
        }

        std::map<std::string, std::vector<cube_p<double>>> ret;
        for ( auto & l: input_nodes_ )
        {
            ret[l.first].resize(0);
        }

        return ret;
    }



    std::pair<std::vector<options>,std::vector<options>> serialize() const
    {
        std::pair<std::vector<options>,std::vector<options>> ret;

        for ( auto & n: nodes_ )
            ret.first.push_back(n.second->nodes->serialize());

        for ( auto & e: edges_ )
            ret.second.push_back(e.second->edges->serialize());

        return ret;
    }

    void zap()
    {
    }
};

}}} // namespace znn::v4::trivial_network
