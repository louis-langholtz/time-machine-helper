#ifndef SORTINGDISABLER_H
#define SORTINGDISABLER_H

template <class T>
concept SortingEnablable = requires(T *a)
{
    a->setSortingEnabled(false);
};

template <SortingEnablable T>
struct SortingDisabler {
    T *sortable;

    SortingDisabler(T *s): sortable{s} {
        this->sortable->setSortingEnabled(false);
    }

    ~SortingDisabler()
    {
        this->sortable->setSortingEnabled(true);
    }
};

#endif // SORTINGDISABLER_H
