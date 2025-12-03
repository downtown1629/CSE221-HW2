#pragma once

#include <vector>
#include <string>
#include <list>
#include <algorithm>
#include <cstring>

// Piece: 원본(ORIGINAL) 혹은 추가(ADD) 버퍼의 특정 구간을 가리킴
struct Piece {
    enum Source { ORIGINAL, ADD };
    Source source;
    size_t start;
    size_t length;
};

class SimplePieceTable {
    std::string original_buffer; 
    std::string add_buffer;      
    std::list<Piece> pieces;     
    size_t total_length;

public:
    SimplePieceTable() : total_length(0) {
        add_buffer.reserve(1024 * 1024); 
    }
    
    // [수정] 누락되었던 size() 멤버 함수 추가
    size_t size() const {
        return total_length;
    }
    
    void insert(size_t pos, const std::string& s) {
        if (s.empty()) return;

        // 1. Add to Append Buffer
        size_t start_idx = add_buffer.size();
        add_buffer += s;
        Piece new_piece = {Piece::ADD, start_idx, s.size()};

        // 2. Find insert position
        if (pieces.empty()) {
            pieces.push_back(new_piece);
            total_length += s.size();
            return;
        }

        size_t current_pos = 0;
        for (auto it = pieces.begin(); it != pieces.end(); ++it) {
            // Found the piece where insertion happens
            if (current_pos + it->length >= pos) {
                size_t offset = pos - current_pos;
                
                // If inserting in the middle of a piece, split it
                if (offset > 0 && offset < it->length) {
                    Piece right_piece = *it;
                    right_piece.start += offset;
                    right_piece.length -= offset;
                    
                    it->length = offset;
                    
                    // Sequence: [Left(Modified it)] -> [New] -> [Right]
                    auto next_it = std::next(it);
                    pieces.insert(next_it, new_piece);
                    pieces.insert(next_it, right_piece);
                } 
                // Inserting at the start of this piece
                else if (offset == 0) {
                    pieces.insert(it, new_piece);
                } 
                // Inserting at the very end of this piece
                else {
                    pieces.insert(std::next(it), new_piece);
                }
                
                total_length += s.size();
                return;
            }
            current_pos += it->length;
        }
        
        // Appending at the very end of text
        pieces.push_back(new_piece);
        total_length += s.size();
    }

    void erase(size_t pos, size_t len) {
        if (pos >= total_length) return;
        if (pos + len > total_length) len = total_length - pos;
        if (len == 0) return;

        size_t current_pos = 0;
        auto it = pieces.begin();

        // 1. Find start piece
        while (it != pieces.end() && current_pos + it->length <= pos) {
            current_pos += it->length;
            ++it;
        }

        // 2. Handle partial delete at the start point
        size_t offset = pos - current_pos;
        if (offset > 0) {
            // Split the start piece into Left (kept) and Right (candidate for deletion)
            Piece right_part = *it;
            right_part.start += offset;
            right_part.length -= offset; // The part from pos onwards
            
            it->length = offset; // Keep the left part
            
            // Insert the right part after current, then move iterator to it
            it = pieces.insert(std::next(it), right_part);
            current_pos += offset; // Now current_pos == pos
        }

        // 3. Delete pieces until len is 0
        while (len > 0 && it != pieces.end()) {
            if (it->length <= len) {
                // Consume entire piece
                len -= it->length;
                total_length -= it->length;
                it = pieces.erase(it);
            } else {
                // Consume partial piece (from the beginning of this piece)
                it->start += len;
                it->length -= len;
                total_length -= len;
                len = 0;
            }
        }
    }

    char at(size_t index) const {
        size_t current_pos = 0;
        for (const auto& p : pieces) {
            if (current_pos + p.length > index) {
                size_t offset = index - current_pos;
                if (p.source == Piece::ORIGINAL) return original_buffer[p.start + offset];
                else return add_buffer[p.start + offset];
            }
            current_pos += p.length;
        }
        return '\0';
    }
};