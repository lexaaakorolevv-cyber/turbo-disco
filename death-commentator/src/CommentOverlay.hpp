#pragma once

#include <Geode/Geode.hpp>
#include "CommentEngine.hpp"

namespace deathcomment {

void showComment(std::string const& text,
                 Context const& context, Options const& config);
void hideComment();

} // namespace deathcomment
