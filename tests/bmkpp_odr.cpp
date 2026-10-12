// Including the header in a second translation unit catches ODR/link failures.
#include <bmkpp/bmkpp.hpp>
double bmkpp_odr_probe() {
    return bmkpp::summarize({1, 2, 3}).mean_ns;
}
