#include "interpreter.h"

#include <ext/pcre/php_pcre.h>

#include "lexer.h"

int compare(zval* lh, zval* rh);
bool compare_rgxp(zval* lh, zval* rh);
void exec_expression(zval* arr_head, zval* arr_cur, struct ast_node* tok, zval* return_value);
void exec_index_filter(zval* arr_head, zval* arr_cur, struct ast_node* tok, zval* return_value);
void exec_recursive_descent(zval* arr_head, zval* arr_cur, struct ast_node* tok, zval* return_value);
void exec_selector(zval* arr_head, zval* arr_cur, struct ast_node* tok, zval* return_value);
void exec_slice(zval* arr_head, zval* arr_cur, struct ast_node* tok, zval* return_value);
void exec_wildcard(zval* arr_head, zval* arr_cur, struct ast_node* tok, zval* return_value);
zval* evaluate_primary(struct ast_node* src, zval* tmp_dest, zval* arr_head, zval* arr_cur);
bool break_if_result_found(zval* return_value);
void copy_result_or_continue(zval* arr_head, zval* arr_cur, struct ast_node* tok, zval* return_value);
bool evaluate_unary(zval* arr_head, zval* arr_cur, struct ast_node* tok);
bool evaluate_binary(zval* arr_head, zval* arr_cur, struct ast_node* tok);
bool evaluate_expression(zval* arr_head, zval* arr_cur, struct ast_node* tok);
bool can_check_inequality(zval* lhs, zval* rhs);

void eval_ast(zval* arr_head, zval* arr_cur, struct ast_node* tok, zval* return_value) {
  if (tok == NULL) {
    return;
  }
  switch (tok->type) {
    case AST_INDEX_LIST:
      exec_index_filter(arr_head, arr_cur, tok, return_value);
      break;
    case AST_INDEX_SLICE:
      exec_slice(arr_head, arr_cur, tok, return_value);
      break;
    case AST_CUR_NODE:
    case AST_ROOT:
      copy_result_or_continue(arr_head, arr_cur, tok, return_value);
      break;
    case AST_RECURSE:
      tok = tok->next;
      exec_recursive_descent(arr_head, arr_cur, tok, return_value);
      break;
    case AST_SELECTOR:
      exec_selector(arr_head, arr_cur, tok, return_value);
      break;
    case AST_WILD_CARD:
      exec_wildcard(arr_head, arr_cur, tok, return_value);
      break;
    case AST_EXPR:
      exec_expression(arr_head, arr_cur, tok, return_value);
      break;
    default:
      assert(0);
      break;
  }
}

void exec_selector(zval* arr_head, zval* arr_cur, struct ast_node* tok, zval* return_value) {
  if (arr_cur == NULL || Z_TYPE_P(arr_cur) != IS_ARRAY) {
    return;
  }

  zend_ulong idx;

  if (ZEND_HANDLE_NUMERIC_STR(tok->data.d_selector.val, tok->data.d_selector.len, idx)) {
    /* look up numeric index */
    arr_cur = zend_hash_index_find(HASH_OF(arr_cur), idx);
  } else {
    /* look up string index */
    arr_cur = zend_hash_str_find(HASH_OF(arr_cur), tok->data.d_selector.val, tok->data.d_selector.len);
  }

  if (arr_cur != NULL) {
    copy_result_or_continue(arr_head, arr_cur, tok, return_value);
  }
}

void exec_wildcard(zval* arr_head, zval* arr_cur, struct ast_node* tok, zval* return_value) {
  if (arr_cur == NULL || Z_TYPE_P(arr_cur) != IS_ARRAY) {
    return;
  }

  zval* data;
  zval* zv_dest;
  zend_string* key;
  zend_ulong num_key;

  ZEND_HASH_FOREACH_KEY_VAL(HASH_OF(arr_cur), num_key, key, data) {
    copy_result_or_continue(arr_head, data, tok, return_value);
    if (break_if_result_found(return_value)) {
      break;
    }
  }
  ZEND_HASH_FOREACH_END();
}

void exec_recursive_descent(zval* arr_head, zval* arr_cur, struct ast_node* tok, zval* return_value) {
  if (arr_cur == NULL || Z_TYPE_P(arr_cur) != IS_ARRAY) {
    return;
  }

  zval* data;
  zval* zv_dest;
  zend_string* key;
  zend_ulong num_key;

  eval_ast(arr_head, arr_cur, tok, return_value);

  ZEND_HASH_FOREACH_KEY_VAL(HASH_OF(arr_cur), num_key, key, data) {
    exec_recursive_descent(arr_head, data, tok, return_value);
  }
  ZEND_HASH_FOREACH_END();
}

void exec_index_filter(zval* arr_head, zval* arr_cur, struct ast_node* tok, zval* return_value) {
  int i;

  for (i = 0; i < tok->data.d_list.count; i++) {
    int index = tok->data.d_list.indexes[i];
    if (index < 0) {
      index = zend_hash_num_elements(HASH_OF(arr_cur)) - abs(index);
    }
    zval* data;
    if ((data = zend_hash_index_find(HASH_OF(arr_cur), index)) != NULL) {
      copy_result_or_continue(arr_head, data, tok, return_value);
      if (break_if_result_found(return_value)) {
        break;
      }
    }
  }
}

void exec_slice(zval* arr_head, zval* arr_cur, struct ast_node* tok, zval* return_value) {
  if (arr_cur == NULL || Z_TYPE_P(arr_cur) != IS_ARRAY) {
    return;
  }

  zval* data;
  int i;

  int data_length = zend_hash_num_elements(HASH_OF(arr_cur));

  int range_start = tok->data.d_list.indexes[0];
  int range_end = tok->data.d_list.count > 1 ? tok->data.d_list.indexes[1] : INT_MAX;
  int range_step = tok->data.d_list.count > 2 ? tok->data.d_list.indexes[2] : 1;

  /* Zero-steps are not allowed, abort */
  if (range_step == 0) {
    return;
  }

  /* Replace placeholder with actual value */
  if (range_start == INT_MAX) {
    range_start = range_step > 0 ? 0 : data_length - 1;
  }
  /* Indexing from the end of the list */
  else if (range_start < 0) {
    range_start = data_length - abs(range_start);
  }

  /* Replace placeholder with actual value */
  if (range_end == INT_MAX) {
    range_end = range_step > 0 ? data_length : -1;
  }
  /* Indexing from the end of the list */
  else if (range_end < 0) {
    range_end = data_length - abs(range_end);
  }

  /* Set suitable boundaries for start index */
  range_start = range_start < -1 ? -1 : range_start;
  range_start = range_start > data_length ? data_length : range_start;

  /* Set suitable boundaries for end index */
  range_end = range_end < -1 ? -1 : range_end;
  range_end = range_end > data_length ? data_length : range_end;

  if (range_step > 0) {
    /* Make sure that the range is sane so we don't end up in an infinite loop */
    if (range_start >= range_end) {
      return;
    }

    for (i = range_start; i < range_end; i += range_step) {
      if ((data = zend_hash_index_find(HASH_OF(arr_cur), i)) != NULL) {
        copy_result_or_continue(arr_head, data, tok, return_value);
        if (break_if_result_found(return_value)) {
          break;
        }
      }
    }
  } else {
    /* Make sure that the range is sane so we don't end up in an infinite loop */
    if (range_start <= range_end) {
      return;
    }

    for (i = range_start; i > range_end; i += range_step) {
      if ((data = zend_hash_index_find(HASH_OF(arr_cur), i)) != NULL) {
        copy_result_or_continue(arr_head, data, tok, return_value);
        if (break_if_result_found(return_value)) {
          break;
        }
      }
    }
  }
}

void exec_expression(zval* arr_head, zval* arr_cur, struct ast_node* tok, zval* return_value) {
  if (arr_cur == NULL || Z_TYPE_P(arr_cur) != IS_ARRAY) {
    return;
  }

  zend_ulong num_key;
  zend_string* key;
  zval* data;

  ZEND_HASH_FOREACH_KEY_VAL(HASH_OF(arr_cur), num_key, key, data) {
    if (evaluate_expression(arr_head, data, tok->data.d_expression.head)) {
      copy_result_or_continue(arr_head, data, tok, return_value);
      if (break_if_result_found(return_value)) {
        break;
      }
    }
  }
  ZEND_HASH_FOREACH_END();
}

int compare(zval* lh, zval* rh) {
  zval result;
  ZVAL_NULL(&result);

  compare_function(&result, lh, rh);
  return (int)Z_LVAL(result);
}

bool compare_rgxp(zval* lh, zval* rh) {
  pcre_cache_entry* pce;

  if ((pce = pcre_get_compiled_regex_cache(Z_STR_P(rh))) == NULL) {
    zval_ptr_dtor(rh);
    return false;
  }

  zval retval;
  zval subpats;

  ZVAL_NULL(&retval);
  ZVAL_NULL(&subpats);

  zend_string* s_lh = zend_string_copy(Z_STR_P(lh));

  php_pcre_match_impl(pce, s_lh, &retval, &subpats, 0, 0, 0, 0);

  zend_string_release_ex(s_lh, 0);
  zval_ptr_dtor(&subpats);

  return Z_LVAL(retval) > 0;
}

zval* evaluate_primary(struct ast_node* src, zval* tmp_dest, zval* arr_head, zval* arr_cur) {
  switch (src->type) {
    case AST_BOOL:
      ZVAL_BOOL(tmp_dest, src->data.d_literal.value_bool);
      return tmp_dest;
    case AST_DOUBLE:
      ZVAL_DOUBLE(tmp_dest, src->data.d_double.value);
      return tmp_dest;
    case AST_LITERAL:
      ZVAL_STRINGL(tmp_dest, src->data.d_literal.val, src->data.d_literal.len);
      return tmp_dest;
    case AST_LONG:
      ZVAL_LONG(tmp_dest, src->data.d_long.value);
      return tmp_dest;
    case AST_NULL:
      ZVAL_NULL(tmp_dest);
      return tmp_dest;
    case AST_ROOT:
      ZVAL_INDIRECT(tmp_dest, NULL);
      eval_ast(arr_head, arr_head, src, tmp_dest);
      if (Z_INDIRECT_P(tmp_dest) == NULL) {
        ZVAL_UNDEF(tmp_dest);
        return tmp_dest;
      }
      return Z_INDIRECT_P(tmp_dest);
    case AST_CUR_NODE:
    case AST_SELECTOR:
      ZVAL_INDIRECT(tmp_dest, NULL);
      eval_ast(arr_head, arr_cur, src, tmp_dest);
      if (Z_INDIRECT_P(tmp_dest) == NULL) {
        ZVAL_UNDEF(tmp_dest);
        return tmp_dest;
      }
      return Z_INDIRECT_P(tmp_dest);
    default:
      assert(0);
      return NULL;
  }
}

bool break_if_result_found(zval* return_value) {
  return Z_TYPE_P(return_value) == IS_INDIRECT && Z_INDIRECT_P(return_value) != NULL;
}

void copy_result_or_continue(zval* arr_head, zval* arr_cur, struct ast_node* tok, zval* return_value) {
  if (tok->next == NULL) {
    if (Z_TYPE_P(return_value) == IS_ARRAY) {
      zval tmp;
      ZVAL_COPY_VALUE(&tmp, arr_cur);
      zval_copy_ctor(&tmp);
      add_next_index_zval(return_value, &tmp);
    } else if (Z_TYPE_P(return_value) == IS_INDIRECT) {
      ZVAL_INDIRECT(return_value, arr_cur);
    }
  } else {
    eval_ast(arr_head, arr_cur, tok->next, return_value);
  }
}

bool evaluate_expression(zval* arr_head, zval* arr_cur, struct ast_node* tok) {
  if (is_binary(tok->type)) {
    return evaluate_binary(arr_head, arr_cur, tok);
  }

  if (is_unary(tok->type)) {
    return evaluate_unary(arr_head, arr_cur, tok);
  }

  if (tok->type == AST_SELECTOR || tok->type == AST_CUR_NODE) {
    zval tmp = {0};
    return Z_TYPE_P(evaluate_primary(tok, &tmp, arr_head, arr_cur)) != IS_UNDEF;
  }

  zval tmp = {0};
  zval* val = evaluate_primary(tok->data.d_unary.right, &tmp, arr_head, arr_cur);

  if (Z_TYPE_P(val) == IS_FALSE) {
    return true;
  }

  return false;
}

bool evaluate_unary(zval* arr_head, zval* arr_cur, struct ast_node* tok) {
  zval tmp = {0};

  if (is_unary(tok->data.d_unary.right->type)) {
    return !evaluate_unary(arr_head, arr_cur, tok->data.d_unary.right);
  } else if (is_binary(tok->data.d_unary.right->type)) {
    return !evaluate_binary(arr_head, arr_cur, tok->data.d_unary.right);
  } else if (tok->data.d_unary.right->type == AST_SELECTOR || tok->data.d_unary.right->type == AST_CUR_NODE) {
    /* ?(!@.selector) */
    return Z_TYPE_P(evaluate_primary(tok->data.d_unary.right, &tmp, arr_head, arr_cur)) == IS_UNDEF;
  }

  zval* val = evaluate_primary(tok->data.d_unary.right, &tmp, arr_head, arr_cur);

  if (Z_TYPE_P(val) == IS_FALSE) {
    return true;
  }

  return false;
}

bool evaluate_binary(zval* arr_head, zval* arr_cur, struct ast_node* tok) {
  /* use stack-allocated zvals in order to avoid malloc, if possible */
  zval tmp_lh = {0}, tmp_rh = {0};
  zval *val_lh = &tmp_lh, *val_rh = &tmp_rh;
  struct ast_node *lh_operand = tok->data.d_binary.left, *rh_operand = tok->data.d_binary.right;

  if (is_binary(lh_operand->type)) {
    bool result = evaluate_binary(arr_head, arr_cur, lh_operand);
    ZVAL_BOOL(val_lh, result);
  } else if (is_unary(tok->type)) {
    bool result = evaluate_unary(arr_head, arr_cur, lh_operand);
    ZVAL_BOOL(val_lh, result);
  } else if ((lh_operand->type == AST_SELECTOR || lh_operand->type == AST_CUR_NODE) &&
             (tok->type == AST_OR || tok->type == AST_AND)) {
    /* ?(@.selector <or|and> [operand]) */
    bool exists = Z_TYPE_P(evaluate_primary(lh_operand, &tmp_lh, arr_head, arr_cur)) != IS_UNDEF;
    ZVAL_BOOL(val_lh, exists);
  } else {
    val_lh = evaluate_primary(lh_operand, &tmp_lh, arr_head, arr_cur);
  }

  if (is_binary(rh_operand->type)) {
    bool result = evaluate_binary(arr_head, arr_cur, rh_operand);
    ZVAL_BOOL(val_rh, result);
  } else if (is_unary(tok->type)) {
    bool result = evaluate_unary(arr_head, arr_cur, rh_operand);
    ZVAL_BOOL(val_rh, result);
  } else if ((rh_operand->type == AST_SELECTOR || rh_operand->type == AST_CUR_NODE) &&
             (tok->type == AST_OR || tok->type == AST_AND)) {
    /* ?([operand] <or|and> @.selector) */
    bool exists = Z_TYPE_P(evaluate_primary(rh_operand, &tmp_rh, arr_head, arr_cur)) != IS_UNDEF;
    ZVAL_BOOL(val_rh, exists);
  } else {
    val_rh = evaluate_primary(rh_operand, &tmp_rh, arr_head, arr_cur);
  }

  bool ret = false;

  switch (tok->type) {
    case AST_EQ:
      ret = fast_is_identical_function(val_lh, val_rh);
      break;
    case AST_NE:
      ret = !fast_is_identical_function(val_lh, val_rh);
      break;
    case AST_LT:
      ret = can_check_inequality(val_lh, val_rh) && compare(val_lh, val_rh) < 0;
      break;
    case AST_LTE:
      ret = can_check_inequality(val_lh, val_rh) && compare(val_lh, val_rh) <= 0;
      break;
    case AST_OR:
      ret = (Z_TYPE_P(val_lh) == IS_TRUE) || (Z_TYPE_P(val_rh) == IS_TRUE);
      break;
    case AST_AND:
      ret = (Z_TYPE_P(val_lh) == IS_TRUE) && (Z_TYPE_P(val_rh) == IS_TRUE);
      break;
    case AST_GT:
      ret = can_check_inequality(val_lh, val_rh) && compare(val_lh, val_rh) > 0;
      break;
    case AST_GTE:
      ret = can_check_inequality(val_lh, val_rh) && compare(val_lh, val_rh) >= 0;
      break;
    case AST_RGXP:
      ret = compare_rgxp(val_lh, val_rh);
      break;
    default:
      assert(0);
      break;
  }

  /* clean up strings allocated in evaluate_primary() */

  if (rh_operand->type == AST_LITERAL) {
    zval_ptr_dtor(val_rh);
  }

FREE_LHS:
  if (lh_operand->type == AST_LITERAL) {
    zval_ptr_dtor(val_lh);
  }

  return ret;
}

/* Determine if two zvals can be checked for inequality (>, <, >=, <=). */
/* Specifically forbid comparing strings with numeric values in order to */
/* avoid returning true for scenarios such as 42 > "value". */
bool can_check_inequality(zval* lhs, zval* rhs) {
  bool lhs_is_numeric = (Z_TYPE_P(lhs) == IS_LONG || Z_TYPE_P(lhs) == IS_DOUBLE);
  bool rhs_is_numeric = (Z_TYPE_P(rhs) == IS_LONG || Z_TYPE_P(rhs) == IS_DOUBLE);

  if (lhs_is_numeric && rhs_is_numeric) {
    return true;
  }

  bool lhs_is_string = Z_TYPE_P(lhs) == IS_STRING;
  bool rhs_is_string = Z_TYPE_P(rhs) == IS_STRING;

  return lhs_is_string && rhs_is_string;
}