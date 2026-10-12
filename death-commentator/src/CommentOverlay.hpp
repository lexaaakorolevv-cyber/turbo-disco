#pragma once

#include <Geode/Geode.hpp>
#include "CommentEngine.hpp"

namespace deathcomment {

void showComment(cocos2d::CCNode* parent, std::string const& text,
                 Context const& context, Options const& config);

} // namespace deathcomment
